// $Id: PhraseDictionaryMemoryHashed.cpp 3908 2011-02-28 11:41:08Z pjwilliams $
// vim:tabstop=2

/***********************************************************************
Moses - factored phrase-based language decoder
Copyright (C) 2006 University of Edinburgh

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
***********************************************************************/

#include <fstream>
#include <string>
#include <iterator>
#include <algorithm>
#include <sys/stat.h>
#include "PhraseDictionaryMemoryHashed.h"
#include "FactorCollection.h"
#include "Word.h"
#include "Util.h"
#include "InputFileStream.h"
#include "StaticData.h"
#include "WordsRange.h"
#include "UserMessage.h"

using namespace std;

namespace Moses
{
  
bool PhraseDictionaryMemoryHashed::Load(const std::vector<FactorType> &input
                                  , const std::vector<FactorType> &output
                                  , const string &filePath
                                  , const vector<float> &weight
                                  , size_t tableLimit
                                  , const LMList &languageModels
                                  , float weightWP)
{
  m_input = &input;
  m_output = &output;
  m_weight = &weight;
  m_tableLimit = tableLimit;
  m_languageModels = &languageModels; 
  m_weightWP = weightWP;

  if(m_implementation == MemoryHashedBinary)
    return LoadBinary(filePath);
  else
    return LoadText(filePath);
    
  return false;
}
  
bool PhraseDictionaryMemoryHashed::LoadText(std::string filePath) {
  InputFileStream inFile(filePath);

  string line, prevSourcePhrase = "";
  size_t phr_num = 0;
  size_t line_num = 0;
  size_t numElement = NOT_FOUND; // 3=old format, 5=async format which include word alignment info

  std::vector<std::vector<std::string> > collection;
  m_phraseCoder = new PhraseCoder(m_output, m_feature, m_numScoreComponent,
                                  m_weight, m_weightWP, m_languageModels);
  
  std::cerr << "Reading in phrase table" << std::endl;
  
  StringVector<unsigned char, unsigned long, MmapAllocator> packedTargetPhrases;
  
  std::stringstream targetStream;
  while(getline(inFile, line)) {
    ++line_num;
    std::vector<string> tokens = TokenizeMultiCharSeparator( line , "|||" );
    std::string sourcePhraseString = tokens[0];
    
    if (numElement == NOT_FOUND) {
      // init numElement
      numElement = tokens.size();
      assert(numElement >= 3);
      // extended style: source ||| target ||| scores ||| [alignment] ||| [counts]
    }
  
    if (tokens.size() != numElement) {
      stringstream strme;
      strme << "Syntax error at " << filePath << ":" << line_num;
      UserMessage::Add(strme.str());
      abort();
    }
    
    bool isLHSEmpty = (sourcePhraseString.find_first_not_of(" \t", 0) == string::npos);
    if (isLHSEmpty) {
      TRACE_ERR( filePath << ":" << line_num << ": pt entry contains empty target, skipping\n");
      continue;
    }
    
    if(sourcePhraseString != prevSourcePhrase && prevSourcePhrase != "") {
      ++phr_num;
      if(phr_num % 100000 == 0)
        std::cerr << ".";
      if(phr_num % 5000000 == 0)
        std::cerr << "[" << phr_num << "]" << std::endl;
      
      m_hash.AddKey(Trim(prevSourcePhrase));
      packedTargetPhrases.push_back(m_phraseCoder->packCollection(collection));
      collection.clear();
    }
    collection.push_back(tokens);
    prevSourcePhrase = sourcePhraseString;    
  }
  
  ++phr_num;
  if(phr_num % 100000 == 0)
    std::cerr << ".";
  if(phr_num % 5000000 == 0)
    std::cerr << "[" << phr_num << "]" << std::endl;

  m_hash.AddKey(Trim(prevSourcePhrase));
  packedTargetPhrases.push_back(m_phraseCoder->packCollection(collection));
  
  m_hash.Create();
  m_hash.ClearKeys();
  
  m_phraseCoder->calcHuffmanCodes();
  
  std::cerr << "Compressing target phrases" << std::endl;
  for(size_t i = 0; i < m_hash.GetSize(); i++) {
    if((i+1) % 100000 == 0)
      std::cerr << ".";
    if((i+1) % 5000000 == 0)
      std::cerr << "[" << (i+1) << "]" << std::endl;
    
    m_targetPhrases.push_back(
      m_phraseCoder->encodePackedCollection(packedTargetPhrases[i])
    );
  }
  
  return true;
}

bool PhraseDictionaryMemoryHashed::LoadBinary(std::string filePath) {
    if (FileExists(filePath + ".mph"))
        filePath += ".mph";

    m_phraseCoder = new PhraseCoder(m_output, m_feature, m_numScoreComponent,
                                    m_weight, m_weightWP, m_languageModels);
  
    std::FILE* pFile = std::fopen(filePath.c_str() , "r");
    m_hash.Load(pFile);
    m_phraseCoder->load(pFile);
    m_targetPhrases.load(pFile);
    std::fclose(pFile);
    
    return true;
}

bool PhraseDictionaryMemoryHashed::SaveBinary(std::string filePath) {
    bool ok = true;
    
    std::FILE* pFile = std::fopen(filePath.c_str() , "w");
    m_hash.Save(pFile);
    m_phraseCoder->load(pFile);
    m_targetPhrases.save(pFile);
    std::fclose(pFile);
    
    return ok;
}

TargetPhraseCollection
*PhraseDictionaryMemoryHashed::CreateTargetPhraseCollection(const Phrase
                                                            &sourcePhrase) {
  const std::string& factorDelimiter
    = StaticData::Instance().GetFactorDelimiter();
  
  std::string sourcePhraseString = sourcePhrase.GetStringRep(*m_input);
  size_t index = m_hash[sourcePhraseString];
  
  if(index != m_hash.GetSize()) {  
    TargetPhraseCollection* phraseColl =
      m_phraseCoder->decodeCollection(m_targetPhrases[index], sourcePhrase);
    
    phraseColl->NthElement(m_tableLimit);
    CacheTargetPhraseCollection(phraseColl);
    return phraseColl;
  }
  else
    return NULL;
}

void
PhraseDictionaryMemoryHashed::AddEquivPhrase(const Phrase &source,
                                             const TargetPhrase &targetPhrase) { }

const TargetPhraseCollection*
PhraseDictionaryMemoryHashed::GetTargetPhraseCollection(const Phrase &sourcePhrase) const {
  return const_cast<PhraseDictionaryMemoryHashed*>(this)->CreateTargetPhraseCollection(sourcePhrase);
}

PhraseDictionaryMemoryHashed::~PhraseDictionaryMemoryHashed() {
  if(m_phraseCoder)
    delete m_phraseCoder;
    
  CleanUp();
}

//TO_STRING_BODY(PhraseDictionaryMemoryHashed);


}

