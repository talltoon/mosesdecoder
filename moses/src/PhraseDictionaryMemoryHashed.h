// $Id: PhraseDictionaryMemoryHashed.h 3943 2011-04-04 20:43:02Z pjwilliams $
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

#ifndef moses_PhraseDictionaryMemoryHashed_h
#define moses_PhraseDictionaryMemoryHashed_h

#include <boost/unordered_map.hpp>

#ifdef WITH_THREADS
#ifdef BOOST_HAS_PTHREADS
#include <boost/thread/mutex.hpp>
#endif
#endif

#include "PhraseDictionary.h"
#include "HashIndex.h"
#include "StringVector.h"
#include "Hufftree.h"

namespace Moses
{

class PhraseDictionaryMemoryHashed : public PhraseDictionary
{
protected:
    
#ifdef WITH_THREADS
#ifdef BOOST_HAS_PTHREADS
  boost::mutex m_threadMutex;
  std::map<pthread_t, std::vector<TargetPhraseCollection*> > m_threadSentenceCache;
#endif
#else
  std::vector<TargetPhraseCollection*> m_sentenceCache;
#endif
    
  typedef boost::unordered_map<size_t, size_t> SymbolCounter;
  typedef boost::unordered_map<float, size_t> ScoreCounter;
  typedef boost::unordered_map<unsigned char, size_t> AlignCounter;
  
  SymbolCounter symbolCount;
  ScoreCounter  scoreCount;
  AlignCounter  alignCount; 
  
  HashIndex<std::allocator, MmapAllocator> m_hash;
  StringVector<unsigned char, size_t, std::allocator> m_targetSymbols;
  std::map<std::string, size_t> m_targetSymbolsMap;
  StringVector<unsigned char, size_t, MmapAllocator> m_targetPhrases;
  
  Hufftree<int, size_t>* m_treeSymbols;
  Hufftree<int, float>* m_treeScores;
  Hufftree<int, unsigned char>* m_treeAlignments;

  PhraseTableImplementation m_implementation;
  
  const std::vector<FactorType>* m_input;
  const std::vector<FactorType>* m_output;
  
  const std::vector<float>* m_weight;
  const LMList* m_languageModels;
  float m_weightWP;
     
  void PackTargetPhrase(std::string, std::ostream&);
  void PackScores(std::string, std::ostream&);
  void PackAlignment(std::string, std::ostream&);
  
  std::istream& UnpackTargetPhrase(std::istream&, std::vector<size_t>&) const;
  std::istream& UnpackScores(std::istream&, std::vector<float>&) const;
  std::istream& UnpackAlignment(std::istream&, std::vector<unsigned char>&) const;

  std::istream& UnpackTargetPhrase(std::istream&, TargetPhrase*) const;
  std::istream& UnpackScores(std::istream&, TargetPhrase*) const;
  std::istream& UnpackAlignment(std::istream&, TargetPhrase*) const;

  std::istream& DecompressTargetPhrase(std::istream&, TargetPhrase*) const;
  std::istream& DecompressScores(std::istream&, TargetPhrase*) const;
  std::istream& DecompressAlignment(std::istream&, TargetPhrase*) const;
  
  size_t AddOrGetTargetSymbol(std::string);
  std::string GetTargetSymbol(size_t) const;
  
  TargetPhraseCollection *CreateTargetPhraseCollection(const Phrase &source);

public:
  PhraseDictionaryMemoryHashed(size_t numScoreComponent,
                               PhraseTableImplementation implementation,
                               PhraseDictionaryFeature* feature)
    : PhraseDictionary(numScoreComponent, feature),
    m_implementation(implementation), m_treeSymbols(0),
    m_treeScores(0), m_treeAlignments(0)
  {}
    
  virtual ~PhraseDictionaryMemoryHashed();

  bool Load(const std::vector<FactorType> &input
            , const std::vector<FactorType> &output
            , const std::string &filePath
            , const std::vector<float> &weight
            , size_t tableLimit
            , const LMList &languageModels
            , float weightWP);
  
  bool LoadText(std::string filePath);
  bool LoadBinary(std::string filePath);
  bool SaveBinary(std::string filePath);

  const TargetPhraseCollection *GetTargetPhraseCollection(const Phrase &source) const;

  void AddEquivPhrase(const Phrase &source, const TargetPhrase &targetPhrase);

  void InitializeForInput(const Moses::InputType&) {}
  
  void CacheTargetPhraseCollection(TargetPhraseCollection *tpc) {
    #ifdef WITH_THREADS
    #ifdef BOOST_HAS_PTHREADS
    boost::mutex::scoped_lock lock(m_threadMutex);
    m_threadSentenceCache[pthread_self()].push_back(tpc);
    #endif
    #else
    m_sentenceCache.push_back(tpc);
    #endif
  }
  
  void CleanUp() {
    #ifdef WITH_THREADS
    #ifdef BOOST_HAS_PTHREADS
    boost::mutex::scoped_lock lock(m_threadMutex);
    std::vector<TargetPhraseCollection*> &ref = m_threadSentenceCache[pthread_self()];
    for(std::vector<TargetPhraseCollection*>::iterator it = ref.begin();
        it != ref.end(); it++)
        delete *it;
    std::vector<TargetPhraseCollection*> temp;
    temp.swap(ref);
    #endif
    #else
    for(std::vector<TargetPhraseCollection*>::iterator it = m_sentenceCache.begin();
        it != m_sentenceCache.end(); it++)
        delete *it;
    std::vector<TargetPhraseCollection*> temp;
    temp.swap(m_sentenceCache);
    #endif
  }

  virtual ChartRuleLookupManager *CreateRuleLookupManager(
    const InputType &,
    const ChartCellCollection &) {
    assert(false);
    return 0;
  }

  TO_STRING();

};

}
#endif
