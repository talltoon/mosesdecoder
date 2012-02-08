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

typedef std::pair<float, size_t> Fpair;

struct ScoreSorter {
  bool operator()(Fpair a, Fpair b) {
    
    if(a.second > b.second)
      return true;
    
    if(a.second == b.second && a.first < b.first)
      return true;
    
    return false;
  }
};

struct FloatTrans {
  FloatTrans(boost::unordered_map<float, float> &fm)
    : m_fm(fm)
  {}
  
  float operator()(float a) {
    return m_fm[a];
  }
  
  boost::unordered_map<float, float>& m_fm;
  
};
  
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
  size_t count = 0;
  size_t line_num = 0;
  size_t numElement = NOT_FOUND; // 3=old format, 5=async format which include word alignment info
  
  std::cerr << "Reading in phrase table ";
  
  StringVector<unsigned char, unsigned long, MmapAllocator> packedTargetPhrases;
  
  std::stringstream targetStream;
  while(getline(inFile, line)) {    
    ++line_num;
    if(line_num % 100000 == 0)
      std::cerr << ".";
    if(line_num % 1000000 == 0)
      std::cerr << "[" << line_num << "]";
    std::vector<string> tokens = TokenizeMultiCharSeparator( line , "|||" );
  
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
  
    const std::string &sourcePhraseString = tokens[0];
    const std::string &targetPhraseString = tokens[1];
    const std::string &scoreString = tokens[2];
  
    bool isLHSEmpty = (sourcePhraseString.find_first_not_of(" \t", 0) == string::npos);
    if (isLHSEmpty) {
      TRACE_ERR( filePath << ":" << line_num << ": pt entry contains empty target, skipping\n");
      continue;
    }
    
    if(sourcePhraseString != prevSourcePhrase && prevSourcePhrase != "") {
      m_hash.AddKey(Trim(prevSourcePhrase));
      std::string temp = targetStream.str();

      packedTargetPhrases.push_back(temp);
      
      targetStream.str("");
    }
    prevSourcePhrase = sourcePhraseString;
    
    PackTargetPhrase(targetPhraseString, targetStream);
    PackScores(scoreString, targetStream);
    //if(tokens.size() > 3)
      //PackAlignment(tokens[3], targetStream);
    
    count++;
  }
  
  m_hash.AddKey(Trim(prevSourcePhrase));
  std::string temp = targetStream.str();
  packedTargetPhrases.push_back(temp);
  std::cerr << std::endl;
  
  std::cerr << "Creating Huffman tree for " << symbolCount.size() << " symbols" << std::endl;
  m_treeSymbols = new Hufftree<int, size_t>(symbolCount.begin(), symbolCount.end());
  
  {
    size_t sum = 0, sumall = 0;
    for(SymbolCounter::iterator it = symbolCount.begin(); it != symbolCount.end(); it++) {
      sumall += it->second * m_treeSymbols->encode(it->first).size();
      sum    += it->second;
    }
    std::cerr << double(sumall)/sum << " bits per symbol" << std::endl;
  }
  
  //std::vector<Fpair> freqScores;
  //freqScores.insert(freqScores.begin(), scoreCount.begin(), scoreCount.end());
  //std::sort(freqScores.begin(), freqScores.end(), ScoreSorter());
  //
  //std::vector<float> topScores;
  //size_t max = 500000;
  //for(size_t i = 0; i < max; i++)
  //  topScores.push_back(freqScores[i].first);
  //
  //std::sort(topScores.begin(), topScores.end());
  //boost::unordered_map<float, float> floatMap;
  //
  //ScoreCounter scoreCount2;
  //for(ScoreCounter::iterator it = scoreCount.begin(); it != scoreCount.end(); it++) {
  //  std::vector<float>::iterator found =
  //    std::lower_bound(topScores.begin(), topScores.end(), it->first);
  //    
  //  if(found != topScores.end()) {
  //    scoreCount2[*found] += it->second;
  //    floatMap[it->first] = *found;
  //    
  //    //std::cerr << "Mapping " << it->first << " to " << *found << std::endl;
  //    
  //  }
  //  else {
  //    scoreCount2[topScores.back()] += it->second;
  //    floatMap[it->first] = topScores.back();
  //  }
  //}
  //
  //scoreCount = scoreCount2;
  //FloatTrans floatTrans(floatMap);
  
  std::cerr << "Creating Huffman tree for " << scoreCount.size() << " scores" << std::endl;
  m_treeScores  = new Hufftree<int, float>(scoreCount.begin(), scoreCount.end());
  
  {
    size_t sum = 0, sumall = 0;
    for(ScoreCounter::iterator it = scoreCount.begin(); it != scoreCount.end(); it++) {
      sumall += it->second * m_treeScores->encode(it->first).size();
      sum    += it->second;
    }
    std::cerr << double(sumall)/sum << " bits per score" << std::endl;
  }
  
  //m_treeAlignments = new Hufftree<int, unsigned char>(alignCount.begin(), alignCount.end());
  
  m_hash.Create();
  
  std::cerr << "Mapping hash values ";
  std::vector<size_t> map(m_hash.GetSize());
  for(size_t i = 0; i < m_hash.GetSize(); i++) {
    if((i+1) % 100000 == 0)
      std::cerr << ".";
    if((i+1) % 1000000 == 0)
      std::cerr << "[" << (i+1) << "]";  
    size_t j = m_hash.GetHashByIndex(i);
    if(j != m_hash.GetSize())
        map[j] = i;
  }
  std::cerr << std::endl;
  
  std::cerr << "Reordering and compressing target phrases ";
  for(size_t i = 0; i < map.size(); i++) {
    if((i+1) % 100000 == 0)
      std::cerr << ".";
    if((i+1) % 1000000 == 0)
      std::cerr << "[" << (i+1) << "]";
    
    std::stringstream packedPhrase(packedTargetPhrases[map[i]].str());
    packedPhrase.unsetf(std::ios::skipws);
    
    std::vector<size_t> symbols;
    std::vector<float>  scores;
    std::vector<unsigned char> alignment;
    
    std::stringstream compressedPhrase;
    compressedPhrase.unsetf(std::ios::skipws);
    while(UnpackTargetPhrase(packedPhrase, symbols) &&  UnpackScores(packedPhrase, scores)) {
          //&& UnpackAlignment(packedPhrase, alignment))
      
      //std::cerr << "1: " << scores.size() << std::endl;
      //for(std::vector<float>::iterator it = scores.begin(); it != scores.end(); it++)
      //  std::cerr << *it << " ";
      //std::cerr << std::endl;
        
      //std::transform(scores.begin(), scores.end(), scores.begin(), floatTrans);

      //std::cerr << "2: " << scores.size() << std::endl;
      //for(std::vector<float>::iterator it = scores.begin(); it != scores.end(); it++)
      //  std::cerr << *it << " ";
      //std::cerr << std::endl;
      
      compressedPhrase
        << m_treeSymbols->encodeWithLength(symbols.begin(), symbols.end())
        << m_treeScores->encode(scores.begin(), scores.end());
        //<< m_treeAlignments->encodeWithLength(alignment.begin(), alignment.end());  
    }
    m_targetPhrases.push_back(compressedPhrase.str());
  }
  std::cerr << std::endl;
  m_hash.ClearKeys();
 
  return true;
}

bool PhraseDictionaryMemoryHashed::LoadBinary(std::string filePath) {
    if (FileExists(filePath + ".mph"))
        filePath += ".mph";
  
    std::FILE* pFile = std::fopen(filePath.c_str() , "r");
    //std::cerr << "1: " << std::ftell(pFile) << std::endl;
    m_hash.Load(pFile);
    //std::cerr << "2: " << std::ftell(pFile) << std::endl;
    m_targetSymbols.load(pFile);
    //std::cerr << "3: " << std::ftell(pFile) << std::endl;
    m_treeSymbols   = new Hufftree<int, size_t>(pFile);
    //std::cerr << "4: " << std::ftell(pFile) << std::endl;
    m_treeScores    = new Hufftree<int, float>(pFile);
    //std::cerr << "5: " << std::ftell(pFile) << std::endl;
    //m_treeAlignments = new Hufftree<int, unsigned char>(pFile);
    m_targetPhrases.load(pFile);
    //std::cerr << "6: " << std::ftell(pFile) << std::endl;
    std::fclose(pFile);
    
    return true;
}

bool PhraseDictionaryMemoryHashed::SaveBinary(std::string filePath) {
    bool ok = true;
    
    std::FILE* pFile = std::fopen(filePath.c_str() , "w");
    //std::cerr << "1: " << std::ftell(pFile) << std::endl;
    m_hash.Save(pFile);
    //std::cerr << "2: " << std::ftell(pFile) << std::endl;
    m_targetSymbols.save(pFile);
    //std::cerr << "3: " << std::ftell(pFile) << std::endl;
    m_treeSymbols->Save(pFile);
    //std::cerr << "4: " << std::ftell(pFile) << std::endl;
    m_treeScores->Save(pFile);
    //std::cerr << "5: " << std::ftell(pFile) << std::endl;
    //m_treeAlignments->Save(pFile);
    m_targetPhrases.save(pFile);
    //std::cerr << "6: " << std::ftell(pFile) << std::endl;
    std::fclose(pFile);
    
    return ok;
}

size_t PhraseDictionaryMemoryHashed::AddOrGetTargetSymbol(std::string symbol) {
  std::map<std::string, size_t>::iterator it = m_targetSymbolsMap.find(symbol);
  if(it != m_targetSymbolsMap.end()) {
    return it->second;
  }
  else {
    size_t value = m_targetSymbols.size();
    m_targetSymbolsMap[symbol] = value;
    m_targetSymbols.push_back(symbol);
    return value;
  }
}

std::string PhraseDictionaryMemoryHashed::GetTargetSymbol(size_t idx) const {
  if(idx < m_targetSymbols.size())
    return m_targetSymbols[idx].str();
  return std::string("##ERROR##");
}

void PhraseDictionaryMemoryHashed::PackTargetPhrase(std::string targetPhrase, std::ostream& os) {
  std::vector<std::string> words = Tokenize(targetPhrase);
  unsigned char c = (unsigned char) words.size();
  os.write((char*) &c, 1);
  
  for(size_t i = 0; i < words.size(); i++) {
    size_t idx = AddOrGetTargetSymbol(words[i]);
    os.write((char*)&idx, sizeof(idx));
    symbolCount[idx]++;
  }
  symbolCount[c]++;
}

void PhraseDictionaryMemoryHashed::PackScores(std::string scores, std::ostream& os) {
  std::stringstream ss(scores);
  float score;
  size_t c = 0;
  while(ss >> score) {
    score = FloorScore(TransformScore(score));
    os.write((char*)&score, sizeof(score));
    scoreCount[score]++;
    c++;
  }
  //scoreCount[c]++;
}

void PhraseDictionaryMemoryHashed::PackAlignment(std::string alignment, std::ostream& os) {
  std::vector<size_t> positions = Tokenize<size_t>(alignment, " \t-");
  unsigned char c = positions.size();
  os.write((char*) &c, 1);
  for(size_t i = 0; i < positions.size(); i++) {
    unsigned char position = positions[i];
    os.write((char*)&position, sizeof(position));
    alignCount[position]++;
  }
  alignCount[c]++;
}

std::istream& PhraseDictionaryMemoryHashed::UnpackTargetPhrase(std::istream& is, std::vector<size_t>& symbols) const {
    unsigned char c;
    if(is.read((char*) &c, 1)) {
      symbols.resize(int(c));
      is.read((char*) &symbols[0], sizeof(size_t) * int(c));
    }   
    return is;
}

std::istream& PhraseDictionaryMemoryHashed::UnpackTargetPhrase(std::istream& is, TargetPhrase* targetPhrase) const {
  std::vector<size_t> symbols;
  UnpackTargetPhrase(is, symbols);  
  for(std::vector<size_t>::iterator it = symbols.begin(); it != symbols.end(); it++) {
    Word word;
    word.CreateFromString(Output, *m_output, GetTargetSymbol(*it), false);
    targetPhrase->AddWord(word);
  }  
  return is;
}

std::istream& PhraseDictionaryMemoryHashed::UnpackScores(std::istream& is, std::vector<float>& scores) const {
  scores.resize(m_numScoreComponent);
  is.read((char*) &scores[0], sizeof(float) * m_numScoreComponent);
  return is;
}

std::istream& PhraseDictionaryMemoryHashed::UnpackScores(std::istream& is, TargetPhrase* targetPhrase) const {
  std::vector<float> scores;
  UnpackScores(is, scores);
  targetPhrase->SetScore(m_feature, scores, *m_weight, m_weightWP, *m_languageModels);
  return is;
}

std::istream& PhraseDictionaryMemoryHashed::UnpackAlignment(std::istream& is, std::vector<unsigned char>& alignment) const {
  unsigned char c;
  if(is.read((char*) &c, 1)) {
    alignment.resize(int(c));
    is.read((char*) &alignment[0], sizeof(unsigned char) * int(c));
  }
  return is;
}

std::istream& PhraseDictionaryMemoryHashed::UnpackAlignment(std::istream& is, TargetPhrase* targetPhrase) const {
  std::vector<unsigned char> alignment;
  UnpackAlignment(is, alignment);

  std::set<std::pair<size_t, size_t> > alignment2;
  for(size_t i = 0; i < alignment.size(); i++) 
    if(i % 2 == 1) 
      alignment2.insert(std::make_pair(alignment[i-1], alignment[i]));
      
  targetPhrase->SetAlignmentInfo(alignment2);
  
  return is;
}

std::istream& PhraseDictionaryMemoryHashed::DecompressTargetPhrase(std::istream& is, TargetPhrase* targetPhrase) const {
  std::vector<size_t> symbols;
  std::istream_iterator<char, char> isit(is), eof;
  m_treeSymbols->decodeWithLength(isit, eof, std::back_inserter(symbols));
  
  for(std::vector<size_t>::iterator it = symbols.begin(); it != symbols.end(); it++) {
    Word word;
    word.CreateFromString(Output, *m_output, GetTargetSymbol(*it), false);
    targetPhrase->AddWord(word);
  }
  return is;
}

std::istream& PhraseDictionaryMemoryHashed::DecompressScores(std::istream& is, TargetPhrase* targetPhrase) const {

  std::vector<float> scores;
  std::istream_iterator<char, char> isit(is), eof;
  m_treeScores->decode(m_numScoreComponent, isit, eof, std::back_inserter(scores));
  
  targetPhrase->SetScore(m_feature, scores, *m_weight, m_weightWP, *m_languageModels);

  return is;
}

std::istream& PhraseDictionaryMemoryHashed::DecompressAlignment(std::istream& is, TargetPhrase* targetPhrase) const {
  std::vector<unsigned char> alignment;
  std::istream_iterator<char, char> isit(is), eof;
  m_treeScores->decodeWithLength(isit, eof, std::back_inserter(alignment));
  std::cerr << alignment.size() << std::endl;
  std::set<std::pair<size_t, size_t> > alignment2;
  for(size_t i = 0; i < alignment.size(); i++) {
    if(i % 2 == 1) 
      alignment2.insert(std::make_pair(alignment[i-1], alignment[i]));
  }
  targetPhrase->SetAlignmentInfo(alignment2);
  
  return is;
}

TargetPhraseCollection *PhraseDictionaryMemoryHashed::CreateTargetPhraseCollection(const Phrase &sourcePhrase) {
  const std::string& factorDelimiter = StaticData::Instance().GetFactorDelimiter();
  
  std::string sourcePhraseString = sourcePhrase.GetStringRep(*m_input);
  size_t index = m_hash[sourcePhraseString];
  
  if(index != m_hash.GetSize()) {  
    TargetPhraseCollection* phraseColl = new TargetPhraseCollection();
           
    std::stringstream tp(m_targetPhrases[index]);
    tp.unsetf(std::ios::skipws);
    
    TargetPhrase* targetPhrase = new TargetPhrase(Output);
    targetPhrase->SetSourcePhrase(&sourcePhrase);
    
    while(DecompressTargetPhrase(tp, targetPhrase)
          && DecompressScores(tp, targetPhrase)) {
          //&& DecompressAlignment(tp, targetPhrase)) {
      
      phraseColl->Add(targetPhrase);
      
      targetPhrase = new TargetPhrase(Output);
      targetPhrase->SetSourcePhrase(&sourcePhrase);    
    }
    delete targetPhrase;
    
    phraseColl->NthElement(m_tableLimit);
    CacheTargetPhraseCollection(phraseColl);
    return phraseColl;
  }
  else
    return NULL;
}

void PhraseDictionaryMemoryHashed::AddEquivPhrase(const Phrase &source, const TargetPhrase &targetPhrase) { }

const TargetPhraseCollection *PhraseDictionaryMemoryHashed::GetTargetPhraseCollection(const Phrase &sourcePhrase) const {
  return const_cast<PhraseDictionaryMemoryHashed*>(this)->CreateTargetPhraseCollection(sourcePhrase);
}

PhraseDictionaryMemoryHashed::~PhraseDictionaryMemoryHashed() {
  if(m_treeSymbols)
    delete m_treeSymbols;

  if(m_treeScores)
    delete m_treeScores;
    
  CleanUp();
}

//TO_STRING_BODY(PhraseDictionaryMemoryHashed);


}

