#ifndef PHRASECODER_H__
#define PHRASECODER_H__

#include <sstream>
#include <vector>
#include <boost/unordered_map.hpp>
#include <fstream>
#include <string>
#include <iterator>
#include <algorithm>
#include <sys/stat.h>

#include "TypeDef.h"
#include "PhraseDictionaryMemoryHashed.h"
#include "FactorCollection.h"
#include "Word.h"
#include "Util.h"
#include "InputFileStream.h"
#include "StaticData.h"
#include "WordsRange.h"
#include "UserMessage.h"
#include "StringVector.h"
#include "Hufftree.h"

namespace Moses {

template <typename DataType> 
class Counter {
  public:
    typedef boost::unordered_map<DataType, size_t> FreqMap;
    typedef typename FreqMap::iterator iterator;
    typedef typename FreqMap::mapped_type mapped_type;
    typedef typename FreqMap::value_type value_type; 

  private:
    FreqMap m_freqMap;
    size_t m_maxSize;
    std::vector<DataType> m_bestVec;
    
    struct FreqSorter {
      bool operator()(const value_type& a, const value_type& b) const {
        
        if(a.second > b.second)
          return true;
        
        // Check impact on translation quality!
        if(a.second == b.second && a.first > b.first)
          return true;
        
        return false;
      }
    };
  
  public:
    Counter() : m_maxSize(0) {}
    
    iterator begin() {
      return m_freqMap.begin();
    }
    
    iterator end() {
      return m_freqMap.end();
    }
    
    void increase(DataType data) {
      m_freqMap[data]++;
    }
  
    void increaseBy(DataType data, size_t num) {
      m_freqMap[data] += num;
    }
    
    mapped_type& operator[](DataType data) {
      return m_freqMap[data];
    }
    
    size_t size() {
      return m_freqMap.size();
    }
    
    void limitSize(size_t maxSize) {
      m_maxSize = maxSize;
      std::vector<std::pair<DataType, mapped_type> > freqVec;
      freqVec.insert(freqVec.begin(), m_freqMap.begin(), m_freqMap.end());
      std::sort(freqVec.begin(), freqVec.end(), FreqSorter());
      
      for(size_t i = 0; i < freqVec.size() && i < m_maxSize; i++)
        m_bestVec.push_back(freqVec[i].first);
        
      std::sort(m_bestVec.begin(), m_bestVec.end());
      
      FreqMap t_freqMap;
      for(typename std::vector<std::pair<DataType, mapped_type> >::iterator it
          = freqVec.begin(); it != freqVec.end(); it++) {
        DataType closest = lowerBound(it->first);
        t_freqMap[closest] += it->second;
      }
      
      m_freqMap.swap(t_freqMap);   
    }
    
    DataType lowerBound(DataType data) {
      if(m_maxSize == 0 || m_bestVec.size() == 0)
        return data;
      else {
        typename std::vector<DataType>::iterator it
          = std::lower_bound(m_bestVec.begin(), m_bestVec.end(), data);
        if(it != m_bestVec.end())
          return *it;
        else
          return m_bestVec.back();
      }
    }
};

class PhraseCoder {
  private:
    typedef Counter<size_t> SymbolCounter;
    typedef Counter<float> ScoreCounter;
    
    typedef std::pair<size_t, size_t> AlignPoint;
    typedef Counter<AlignPoint> AlignCounter;
    
    typedef std::pair<unsigned, unsigned> SrcTrg;
    typedef std::pair<std::string, std::string> SrcTrgString;
    typedef std::pair<SrcTrgString, float> SrcTrgProb;
    
    bool m_compactEncoding;
    
    bool m_treeForEachScore;
    size_t m_maxScores;
    bool m_containsAlignmentInfo;
    
    size_t m_numScoreComponent;
    
    SymbolCounter m_symbolCount;
    std::vector<ScoreCounter> m_scoreCounts;
    AlignCounter m_alignCount;
    
    std::vector<size_t> m_lexicalTableIndex;
    std::vector<SrcTrg> m_lexicalTable;
  
    StringVector<unsigned char, unsigned, std::allocator> m_sourceSymbols;
    boost::unordered_map<std::string, unsigned> m_sourceSymbolsMap;
    
    StringVector<unsigned char, unsigned, std::allocator> m_targetSymbols;
    boost::unordered_map<std::string, unsigned> m_targetSymbolsMap;
    
    Hufftree<int, unsigned>* m_symbolTree;
    std::vector<Hufftree<int, float>*> m_scoreTrees;
    Hufftree<int, AlignPoint>* m_alignTree;
    
    // ***********************************************
    
    const std::vector<FactorType>* m_input;
    const std::vector<FactorType>* m_output;
    const PhraseDictionaryFeature* m_feature;
    const std::vector<float>* m_weight;
    float m_weightWP;
    const LMList* m_languageModels;
  
    // ***********************************************
  
    unsigned AddOrGetSourceSymbol(std::string symbol) {
      boost::unordered_map<std::string, unsigned>::iterator it = m_sourceSymbolsMap.find(symbol);
      if(it != m_sourceSymbolsMap.end()) {
        return it->second;
      }
      else {        
        size_t idx = m_sourceSymbols.find(symbol);
        if(idx < m_sourceSymbols.size())
          return idx;
      
        unsigned value = m_sourceSymbols.size();
        m_sourceSymbolsMap[symbol] = value;
        m_sourceSymbols.push_back(symbol);
        return value;
      }
    }

    unsigned AddOrGetTargetSymbol(std::string symbol) {
      boost::unordered_map<std::string, unsigned>::iterator it = m_targetSymbolsMap.find(symbol);
      if(it != m_targetSymbolsMap.end()) {
        return it->second;
      }
      else {
        unsigned value = m_targetSymbols.size();
        m_targetSymbolsMap[symbol] = value;
        m_targetSymbols.push_back(symbol);
        return value;
      }
    }
    
    std::string GetTargetSymbol(unsigned idx) const {
      if(idx < m_targetSymbols.size())
        return m_targetSymbols[idx];
      return std::string("##ERROR##");
    }
    
    size_t getRank(unsigned srcIdx, unsigned trgIdx) {
      size_t srcTrgIdx = m_lexicalTableIndex[srcIdx];
      while(srcIdx == m_lexicalTable[srcTrgIdx].first
        && m_lexicalTable[srcTrgIdx].second != trgIdx)
        srcTrgIdx++;
      return srcTrgIdx - m_lexicalTableIndex[srcIdx];
    }
    
    unsigned getTranslation(unsigned srcIdx, size_t rank) {
      size_t srcTrgIdx = m_lexicalTableIndex[srcIdx];
      return m_lexicalTable[srcTrgIdx + rank].second;
    }
    
    unsigned encode1(size_t rank) {
      unsigned symbol = rank;
      symbol |= 1 << 30;
      return symbol;
    }

    unsigned encode2(size_t pos, size_t rank) {
      unsigned symbol = rank;
      symbol |= 2 << 30;
      symbol |= pos << 24;
      return symbol;
    }

    unsigned encode3(unsigned trgIdx) {
      unsigned symbol = trgIdx;
      symbol |= 3 << 30;
      return symbol;
    }
    
    size_t getType(unsigned encodedSymbol) {
      return encodedSymbol >> 30;
    }
    
    unsigned decodeSymbol1(unsigned encodedSymbol) {
      return encodedSymbol &= ~(1 << 30);
    }
    
    unsigned decodeSymbol2(unsigned encodedSymbol) {
      return encodedSymbol &= ~(255 << 24);
    }
    
    unsigned decodeSymbol3(unsigned encodedSymbol) {
      return encodedSymbol &= ~(3 << 30);
    }
    
    size_t decodeSourcePosition(unsigned encodedSymbol) {
      encodedSymbol &= ~(3 << 30);
      encodedSymbol >>= 24;
      return encodedSymbol;
    }
    
    struct SrcTrgProbSorter {
      bool operator()(const SrcTrgProb& a, const SrcTrgProb& b) const {
        
        if(a.first.first < b.first.first)
          return true;
        
        // Check impact on translation quality!
        if(a.first.first == b.first.first && a.second > b.second)
          return true;
        
        if(a.first.first == b.first.first
           && a.second == b.second
           && a.first.second < b.first.second
        )
          return true;
        
        return false;
      }
    };
    
    std::set<AlignPoint> packTargetPhraseLex(
      std::string sourcePhrase, std::string targetPhrase,
      std::string alignment, std::ostream& os) {
      
      //std::cerr << sourcePhrase << "||| " << targetPhrase << "||| " << alignment << std::endl;
      
      std::vector<std::string> s = Tokenize(sourcePhrase);
      std::vector<std::string> t = Tokenize(targetPhrase);
      std::set<AlignPoint> a;
      
      std::vector<size_t> positions = Tokenize<size_t>(alignment, " \t-");
      std::vector<std::vector<size_t> > a2(t.size());
      for(size_t i = 0; i < positions.size(); i += 2) {
        a.insert(AlignPoint(positions[i], positions[i+1]));
        a2[positions[i+1]].push_back(positions[i]);
      }
      
      std::stringstream encTargetPhrase;
      
      for(size_t i = 0; i < t.size(); i++) {
        unsigned idxTarget = AddOrGetTargetSymbol(t[i]);
        //std::cerr << i << " " << t[i] << " " << idxTarget << std::endl;
        unsigned packedSymbol = -1;
        
        size_t bestSrcPos = s.size();
        size_t bestTrgPos = t.size();
        size_t bestRank = m_lexicalTable.size();
        
        for(std::vector<size_t>::iterator it = a2[i].begin(); it != a2[i].end(); it++) {
          unsigned idxSource = AddOrGetSourceSymbol(s[*it]);
          //std::cerr << "Aligned with " << s[*it] << " " << idxSource << std::endl;
          size_t r = getRank(idxSource, idxTarget);
          if(r < bestRank) {
            bestRank = r;
            bestSrcPos = *it;
            bestTrgPos = i;
          }
          else if(r == bestRank && *it < bestSrcPos) {
            bestSrcPos = *it;
            bestTrgPos = i;
          }
        
        }
        
        if(bestSrcPos < s.size()) {
          if(bestSrcPos == i)
            packedSymbol = encode1(bestRank);
          else
            packedSymbol = encode2(bestSrcPos, bestRank);
            
          a.erase(AlignPoint(bestSrcPos, bestTrgPos));
        }
        else
          packedSymbol = encode3(idxTarget);
        
        //std::cerr << "packed: " << packedSymbol << std::endl;
        os.write((char*)&packedSymbol, sizeof(packedSymbol));
        m_symbolCount[packedSymbol]++;
      }
      
      unsigned stpIdx = AddOrGetTargetSymbol("__SPECIAL_STOP_SYMBOL__");
      os.write((char*)&stpIdx, sizeof(stpIdx));
      m_symbolCount[stpIdx]++;
      
      return a;
    }

    void packTargetPhrase(std::string targetPhrase, std::ostream& os) {
      std::vector<std::string> words = Tokenize(targetPhrase);
      unsigned stpIdx = AddOrGetTargetSymbol("__SPECIAL_STOP_SYMBOL__");
      for(size_t i = 0; i < words.size(); i++) {
        unsigned idx = AddOrGetTargetSymbol(words[i]);
        os.write((char*)&idx, sizeof(idx));
        m_symbolCount[idx]++;
      }
      os.write((char*)&stpIdx, sizeof(stpIdx));
      m_symbolCount[stpIdx]++;
    }

    void packScores(std::string scores, std::ostream& os) {
      std::stringstream ss(scores);
      float score;
      size_t c = 0;
      size_t scoreTypes = m_scoreCounts.size();
      while(ss >> score) {
        score = FloorScore(TransformScore(score));
        os.write((char*)&score, sizeof(score));
        m_scoreCounts[c % scoreTypes][score]++;
        c++;
      }
    }
    
    void packAlignment(std::string alignment, std::ostream& os) {
      std::vector<size_t> positions = Tokenize<size_t>(alignment, " \t-");
      
      for(size_t i = 0; i < positions.size(); i += 2) {
        AlignPoint ap(positions[i], positions[i+1]);
        os.write((char*)&ap, sizeof(AlignPoint));
        m_alignCount[ap]++;
      }
      AlignPoint stop(-1, -1);
      m_alignCount[stop]++;
      os.write((char*) &stop, sizeof(AlignPoint));
    }
  
    void packAlignment(std::set<AlignPoint>& alignment, std::ostream& os) {
      for(std::set<AlignPoint>::iterator it = alignment.begin();
          it != alignment.end(); it++) {
        os.write((char*)&(*it), sizeof(AlignPoint));
        m_alignCount[*it]++;
      }
      AlignPoint stop(-1, -1);
      m_alignCount[stop]++;
      os.write((char*) &stop, sizeof(AlignPoint));
    }

  public:
    typedef std::vector<std::vector<std::string> > StringCollection;
    
    PhraseCoder(
      const std::vector<FactorType>* &input,
      const std::vector<FactorType>* &output,
      const PhraseDictionaryFeature* feature,
      size_t numScoreComponent,
      const std::vector<float>* weight,
      float weightWP,
      const LMList* languageModels
    )
    : m_compactEncoding(false), m_treeForEachScore(true), m_maxScores(100000),
      m_containsAlignmentInfo(true),
      m_symbolTree(0), m_alignTree(0),
      m_input(input), m_output(output), m_feature(feature),
      m_numScoreComponent(numScoreComponent),
      m_weight(weight), m_weightWP(weightWP),
      m_languageModels(languageModels)
    {
      m_scoreCounts.resize(m_treeForEachScore ? m_numScoreComponent : 1);
      m_scoreTrees.resize(m_treeForEachScore ? m_numScoreComponent : 1, 0);
    }
    
    ~PhraseCoder() {
      if(m_symbolTree)
        delete m_symbolTree;
      
      for(size_t i = 0; i < m_scoreTrees.size(); i++)
        if(m_scoreTrees[i])
          delete m_scoreTrees[i];
      
      if(m_alignTree)
        delete m_alignTree;
    }
    
    void loadLexicalTable(std::string filePath) {
      std::vector<SrcTrgProb> t_lexTable;
      
      AddOrGetTargetSymbol("__SPECIAL_STOP_SYMBOL__");

      std::cerr << "Reading in lexical table from " << filePath << std::endl;
      std::ifstream lexIn(filePath.c_str(), std::ifstream::in);
      std::string src, trg;
      float prob;
      
      while(lexIn >> trg >> src >> prob) {
        if(t_lexTable.size() % 10000 == 0)
          std::cerr << ".";
        t_lexTable.push_back(SrcTrgProb(SrcTrgString(src, trg), prob));
      }
      std::cerr << std::endl;
      
      std::cerr << "Read in " << t_lexTable.size() << " lexical pairs" << std::endl;
      std::sort(t_lexTable.begin(), t_lexTable.end(), SrcTrgProbSorter());
      std::cerr << "Sorted" << std::endl;
      
      std::string srcWord = "";
      size_t srcIdx = 0;
      for(typename std::vector<SrcTrgProb>::iterator it = t_lexTable.begin();
          it != t_lexTable.end(); it++) {
        
        if(it->first.first != srcWord) {
          srcIdx = AddOrGetSourceSymbol(it->first.first);
          if(srcIdx >= m_lexicalTableIndex.size())
            m_lexicalTableIndex.resize(srcIdx + 1);
          m_lexicalTableIndex[srcIdx] = m_lexicalTable.size();
        }
        size_t trgIdx = AddOrGetTargetSymbol(it->first.second);
        
        //std::cerr << it->first.first << " " << it->first.second << it->second << std::endl;
        //std::cerr << srcIdx << " " << trgIdx << std::endl;
        
        if(m_lexicalTable.size() % 10000 == 0)
          std::cerr << ".";
        m_lexicalTable.push_back(SrcTrg(srcIdx, trgIdx));
        
        srcWord = it->first.first;
      }
      std::cerr << std::endl;
    }
    
    size_t load(std::FILE* in) {
      size_t start = std::ftell(in);
      std::fread(&m_compactEncoding, sizeof(bool), 1, in);
      std::fread(&m_treeForEachScore, sizeof(bool), 1, in);
      m_scoreCounts.resize(m_treeForEachScore ? m_numScoreComponent : 1);
      m_scoreTrees.resize(m_treeForEachScore ? m_numScoreComponent : 1, 0);
      
      std::fread(&m_containsAlignmentInfo, sizeof(bool), 1, in);
      
      if(m_compactEncoding && m_containsAlignmentInfo) {
        m_sourceSymbols.load(in);
        
        size_t size;
        std::fread(&size, sizeof(size_t), 1, in);
        m_lexicalTableIndex.resize(size);
        std::fread(&m_lexicalTableIndex[0], sizeof(size_t), size, in);
        
        std::fread(&size, sizeof(size_t), 1, in);
        m_lexicalTable.resize(size);
        std::fread(&m_lexicalTable[0], sizeof(SrcTrg), size, in);
      }
      
      m_targetSymbols.load(in);
      m_symbolTree = new Hufftree<int, unsigned>(in);
      
      size_t numTrees = m_treeForEachScore ? m_numScoreComponent : 1;
      for(int i = 0; i < numTrees; i++)
        m_scoreTrees[i] = new Hufftree<int, float>(in);
      
      if(m_containsAlignmentInfo)
        m_alignTree = new Hufftree<int, AlignPoint>(in);
        
      size_t end = std::ftell(in);
      return end - start;
    }
    
    size_t save(std::FILE* out) {
      size_t start = std::ftell(out);
      std::fwrite(&m_compactEncoding, sizeof(bool), 1, out);
      std::fwrite(&m_treeForEachScore, sizeof(bool), 1, out);
      std::fwrite(&m_containsAlignmentInfo, sizeof(bool), 1, out);
      
      if(m_compactEncoding && m_containsAlignmentInfo) {
        m_sourceSymbols.save(out);
        
        size_t size = m_lexicalTableIndex.size();
        std::fwrite(&size, sizeof(size_t), 1, out);
        std::fwrite(&m_lexicalTableIndex[0], sizeof(size_t), size, out);
        
        size = m_lexicalTable.size();
        std::fwrite(&size, sizeof(size_t), 1, out);
        std::fwrite(&m_lexicalTable[0], sizeof(SrcTrg), size, out);
      }
      m_targetSymbols.save(out);
      m_symbolTree->Save(out);
      
      size_t numTrees = m_treeForEachScore ? m_numScoreComponent : 1;
      for(int i = 0; i < numTrees; i++)
        m_scoreTrees[i]->Save(out);
      
      if(m_containsAlignmentInfo)
        m_alignTree->Save(out);
      
      size_t end = std::ftell(out);
      return end - start;
    }
    
    void calcHuffmanCodes() {
      std::cerr << "Creating Huffman codes for " << m_symbolCount.size() << " symbols" << std::endl;
      m_symbolTree = new Hufftree<int, unsigned>(m_symbolCount.begin(), m_symbolCount.end());      
      {
        size_t sum = 0, sumall = 0;
        for(SymbolCounter::iterator it = m_symbolCount.begin(); it != m_symbolCount.end(); it++) {
          sumall += it->second * m_symbolTree->encode(it->first).size();
          sum    += it->second;
        }
        std::cerr << double(sumall)/sum << " bits per symbol" << std::endl;
      }
      
      for(size_t i = 0; i < m_scoreCounts.size(); i++) {
        if(m_scoreCounts.size() > 1)
          std::cerr << "Encoding scores of type " << (i+1) << std::endl;
        
        if(m_maxScores)
          m_scoreCounts[i].limitSize(m_maxScores);
        
        std::cerr << "Creating Huffman codes for " << m_scoreCounts[i].size() << " scores" << std::endl;
        m_scoreTrees[i] = new Hufftree<int, float>(m_scoreCounts[i].begin(), m_scoreCounts[i].end());
        {  
          size_t sum = 0, sumall = 0;
          for(ScoreCounter::iterator it = m_scoreCounts[i].begin(); it != m_scoreCounts[i].end(); it++) {
            sumall += it->second * m_scoreTrees[i]->encode(it->first).size();
            sum    += it->second;
          }
          std::cerr << double(sumall)/sum << " bits per score" << std::endl;
        }
      }
      
      if(m_containsAlignmentInfo) {
        std::cerr << "Creating Huffman codes for " << m_alignCount.size() << " alignment points" << std::endl;
        m_alignTree = new Hufftree<int, AlignPoint>(m_alignCount.begin(), m_alignCount.end());
        {  
          size_t sum = 0, sumall = 0;
          for(AlignCounter::iterator it = m_alignCount.begin(); it != m_alignCount.end(); it++) {
            sumall += it->second * m_alignTree->encode(it->first).size();
            sum    += it->second;
          }
          std::cerr << double(sumall)/sum << " bits per alignment point" << std::endl;
        }
      }
    }
    
    std::string packCollection(StringCollection collection) {
      std::stringstream packedCollectionStream;
      packedCollectionStream.unsetf(std::ios::skipws);
      
      for(StringCollection::iterator it = collection.begin(); it != collection.end(); it++) {
        if(m_compactEncoding && m_containsAlignmentInfo) {
          std::set<AlignPoint> aHat = packTargetPhraseLex(
            (*it)[0], (*it)[1], (*it)[3], packedCollectionStream);
          packScores((*it)[2], packedCollectionStream);
          packAlignment(aHat, packedCollectionStream);
        }
        else {
          packTargetPhrase((*it)[1], packedCollectionStream);
          packScores((*it)[2], packedCollectionStream);
          if(m_containsAlignmentInfo)
            packAlignment((*it)[3], packedCollectionStream);
        }
      }
      
      return packedCollectionStream.str();
    }
    
    std::string encodePackedCollection(std::string packedCollection) {
      
      enum EncodeState {
        ReadSymbol, ReadScore, ReadAlignment,
        EncodeSymbol, EncodeScore, EncodeAlignment };
      EncodeState state = ReadSymbol;
      
      unsigned phraseStopSymbol = m_targetSymbolsMap["__SPECIAL_STOP_SYMBOL__"];
      AlignPoint alignStopSymbol(-1, -1);
      size_t scoreTypes = m_scoreTrees.size();
      
      std::stringstream packedStream(packedCollection);
      packedStream.unsetf(std::ios::skipws);
      std::string result;
      
      char byte = 0;
      char mask = 1;
      unsigned int pos = 0;
      
      unsigned symbol;
      
      float score;
      size_t currScore = 0;
      
      AlignPoint alignPoint;
      
      while(packedStream) {
        switch(state) {
          case ReadSymbol:
            packedStream.read((char*) &symbol, sizeof(unsigned));
            state = EncodeSymbol;
            break;
          case ReadScore:
            if(currScore == m_numScoreComponent) {
              currScore = 0;
              if(m_containsAlignmentInfo)
                state = ReadAlignment;
              else
                state = ReadSymbol;
            }
            else {
              packedStream.read((char*) &score, sizeof(float));
              currScore++;
              state = EncodeScore;
            }
            break;
          case ReadAlignment:
            packedStream.read((char*) &alignPoint, sizeof(AlignPoint));
            state = EncodeAlignment;
            break;
          case EncodeSymbol:
          case EncodeScore:
          case EncodeAlignment:
            std::vector<bool> code;
            if(state == EncodeSymbol) {
              code = m_symbolTree->encode(symbol);
              if(symbol == phraseStopSymbol)
                state = ReadScore;
              else
                state = ReadSymbol;
            }
            else if(state == EncodeScore) {
              size_t idx = (currScore-1) % scoreTypes;
              float closestScore = m_scoreCounts[idx].lowerBound(score);
              code = m_scoreTrees[idx]->encode(closestScore);
              state = ReadScore;
            }
            else if(state == EncodeAlignment) {
              code = m_alignTree->encode(alignPoint);
              if(alignPoint == alignStopSymbol)
                state = ReadSymbol;
              else
                state = ReadAlignment;
            }
            
            for(size_t j = 0; j < code.size(); j++) {
              if(code[j])
                byte |= mask;
              mask = mask << 1;
              pos++;
              
              if(pos % 8 == 0) {
                result.push_back(byte);
                mask = 1;
                byte = 0;
              }
            }
            break;
        }
      }
    
      // Add last byte with remaining waste bits
      if(pos % 8 != 0)
        result.push_back(byte);
      
      return result;
    }
    
    TargetPhraseCollection* decodeCollection(std::string encoded, const Phrase &sourcePhrase) {
      TargetPhraseCollection* phraseColl = new TargetPhraseCollection();
      
      std::cerr << "String size: " << encoded.size() << std::endl;
      
      unsigned phraseStopSymbol = m_targetSymbolsMap["__SPECIAL_STOP_SYMBOL__"];
      AlignPoint alignStopSymbol(-1, -1);
      
      TargetPhrase* targetPhrase;
      std::vector<float> scores;
      std::set<std::pair<size_t, size_t> > alignment;
        
      enum DecodeState { New, Phrase, Score, Alignment, Add } state = New;
      
      int node = 0;
      
      for(std::string::iterator it = encoded.begin(); it != encoded.end(); it++) {
        char byte = *it;
        char mask = 1;
        
        for(int i = 0; i < 8; i++) {
          
          if(state == New || state == Phrase)
            node = (byte & mask) ? m_symbolTree->node(node+1) : m_symbolTree->node(node);
          else if(state == Score) {
            size_t treeNum = scores.size() % m_scoreTrees.size();
            node = (byte & mask) ? m_scoreTrees[treeNum]->node(node+1) : m_scoreTrees[treeNum]->node(node);
          }
          else if(state == Alignment) {
            if(m_containsAlignmentInfo)
              node = (byte & mask) ? m_alignTree->node(node+1) : m_alignTree->node(node);            
          }
          
          if(node < 0) {            
            if(state == New) {
              targetPhrase = new TargetPhrase(Output);
              targetPhrase->SetSourcePhrase(&sourcePhrase);
              
              state = Phrase;
            }
            
            if(state == Phrase) {
              size_t symbol = m_symbolTree->data(-node-1);
              if(symbol == phraseStopSymbol)
                state = Score;
              else {
                std::string strSymbol;
                if(m_compactEncoding && m_containsAlignmentInfo) {
                  std::cerr << "Symbol: " << symbol << std::endl;
                  unsigned decodedSymbol;
                  switch(getType(symbol)) {
                    case 1: {
                        size_t rank = decodeSymbol1(symbol);
                        size_t pos = targetPhrase->GetSize();
                        
                        std::cerr << rank << " " << pos << " - " << sourcePhrase << std::endl;
                        
                        std::string sourceWord
                          = sourcePhrase.GetWord(pos).GetString(*m_input, false);
                          
                        std::cerr << "Retrieving: '" << sourceWord << "'" << std::endl;
                        unsigned idx = AddOrGetSourceSymbol(sourceWord);
                        strSymbol = GetTargetSymbol(getTranslation(idx, rank));
                        std::cerr << "Got: " << strSymbol << std::endl;
                      }
                      break; 
                    case 2: {
                        size_t rank = decodeSymbol2(symbol);
                        size_t pos = decodeSourcePosition(symbol);
                        
                        std::cerr << rank << " " << pos << " - " << sourcePhrase << std::endl;
                        
                        std::string sourceWord
                          = sourcePhrase.GetWord(pos).GetString(*m_input, false);
                        
                        std::cerr << "Retrieving: '" << sourceWord << "'" << std::endl;
                          
                        unsigned idx = AddOrGetSourceSymbol(sourceWord);
                        strSymbol = GetTargetSymbol(getTranslation(idx, rank));
                        std::cerr << "Got: " << strSymbol << std::endl;
                      }
                      break;
                    case 3:
                      decodedSymbol = decodeSymbol3(symbol);
                      strSymbol = GetTargetSymbol(decodedSymbol);
                      std::cerr << strSymbol << std::endl;
                      break;
                  }
                }
                else {
                  strSymbol = GetTargetSymbol(symbol);
                }
                Word word;
                word.CreateFromString(Output, *m_output, strSymbol, false);
                targetPhrase->AddWord(word);
              }
            }
            else if(state == Score) {
              size_t treeNum = scores.size() % m_scoreTrees.size();
              float score = m_scoreTrees[treeNum]->data(-node-1);
              scores.push_back(score);
              
              if(scores.size() == m_numScoreComponent) {
                targetPhrase->SetScore(m_feature, scores, *m_weight, m_weightWP, *m_languageModels);
                scores.clear();
                
                if(m_containsAlignmentInfo)
                  state = Alignment;
                else
                  state = Add;
              }
            }
            else if(state == Alignment) {
              AlignPoint alignPoint = m_alignTree->data(-node-1);
              if(alignPoint == alignStopSymbol) {
                if(StaticData::Instance().UseAlignmentInfo()) {
                  targetPhrase->SetAlignmentInfo(alignment);
                }
                alignment.clear();
                state = Add;
              }
              else {
                alignment.insert(alignPoint);
              }
            }
            
            if(state == Add) {
              phraseColl->Add(targetPhrase);
              state = New;
              if(std::distance(it, encoded.end()) == 1) {
                return phraseColl;
              }
            }
            
            node = 0;
          }
          mask = mask << 1;
        }
      }
      
      return phraseColl;
    }
    
};

}

#endif