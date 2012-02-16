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

class PhraseCoder {
  private:
    typedef boost::unordered_map<size_t, size_t> SymbolCounter;
    typedef boost::unordered_map<float, size_t> ScoreCounter;
    typedef std::pair<unsigned char, unsigned char> AlignPoint;
    typedef boost::unordered_map<AlignPoint, size_t> AlignCounter;
    
    bool m_compactEncoding;
    bool m_treeForEachScore;
    bool m_containsAlignmentInfo;
    
    SymbolCounter m_symbolCount;
    std::vector<ScoreCounter> m_scoreCounts;
    AlignCounter m_alignCount; 
  
    StringVector<unsigned char, unsigned, std::allocator> m_targetSymbols;
    std::map<std::string, unsigned> m_targetSymbolsMap;
    
    Hufftree<int, unsigned>* m_symbolTree;
    std::vector<Hufftree<int, float>*> m_scoreTrees;
    Hufftree<int, AlignPoint>* m_alignTree;
    
    // ***********************************************
    
    const std::vector<FactorType>* m_output;
    const PhraseDictionaryFeature* m_feature;
    size_t m_numScoreComponent;
    const std::vector<float>* m_weight;
    float m_weightWP;
    const LMList* m_languageModels;
  
    // ***********************************************
  
    unsigned AddOrGetTargetSymbol(std::string symbol) {
      std::map<std::string, unsigned>::iterator it = m_targetSymbolsMap.find(symbol);
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
    std::vector<unsigned char> positions
      = Tokenize<unsigned char>(alignment, " \t-");
    
    for(size_t i = 0; i < positions.size(); i+=2) {
      AlignPoint ap(positions[i], positions[i+1]);
      os.write((char*)&ap, sizeof(AlignPoint));
      m_alignCount[ap]++;
    }
    AlignPoint stop(-1, -1);
    m_alignCount[stop]++;
    os.write((char*) &stop, sizeof(AlignPoint));
  }

  public:
    typedef std::vector<std::vector<std::string> > StringCollection;
    
    PhraseCoder(
      const std::vector<FactorType>* &output,
      const PhraseDictionaryFeature* feature,
      size_t numScoreComponent,
      const std::vector<float>* weight,
      float weightWP,
      const LMList* languageModels
    )
    : m_compactEncoding(false), m_treeForEachScore(false),
      m_containsAlignmentInfo(false), m_output(output), m_feature(feature),
      m_numScoreComponent(numScoreComponent),
      m_weight(weight),
      m_weightWP(weightWP), m_languageModels(languageModels),
      m_symbolTree(0),
      m_alignTree(0),
      m_scoreCounts(m_treeForEachScore ? m_numScoreComponent : 1),
      m_scoreTrees(m_treeForEachScore ? m_numScoreComponent : 1, 0)
    { }
    
    ~PhraseCoder() {
      if(m_symbolTree)
        delete m_symbolTree;
      
      for(size_t i = 0; i < m_scoreTrees.size(); i++)
        if(m_scoreTrees[i])
          delete m_scoreTrees[i];
      
      if(m_alignTree)
        delete m_alignTree;
    }
    
    void load(std::FILE* in) {
      std::fread(&m_treeForEachScore, sizeof(bool), 1, in);
      std::fread(&m_containsAlignmentInfo, sizeof(bool), 1, in);
      
      m_targetSymbols.load(in);
      m_symbolTree = new Hufftree<int, unsigned>(in);
      
      size_t numTrees = m_treeForEachScore ? m_numScoreComponent : 1;
      for(int i = 0; i < numTrees; i++)
        m_scoreTrees.push_back(new Hufftree<int, float>(in));
      
      if(m_containsAlignmentInfo)
        m_alignTree = new Hufftree<int, AlignPoint>(in);
    }
    
    void save(std::FILE* out) {
      std::fwrite(&m_treeForEachScore, sizeof(bool), 1, out);
      std::fwrite(&m_containsAlignmentInfo, sizeof(bool), 1, out);
      
      m_targetSymbols.save(out);
      m_symbolTree->Save(out);
      
      size_t numTrees = m_treeForEachScore ? m_numScoreComponent : 1;
      for(int i = 0; i < numTrees; i++)
        m_scoreTrees[i]->Save(out);
      
      if(m_containsAlignmentInfo)
        m_alignTree->Save(out);
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
        packTargetPhrase((*it)[1], packedCollectionStream);
        packScores((*it)[2], packedCollectionStream);
        if(m_containsAlignmentInfo)
          packAlignment((*it)[3], packedCollectionStream);        
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
              state = ReadAlignment;
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
              code = m_scoreTrees[(currScore-1) % scoreTypes]->encode(score);
              state = ReadScore;
            }
            else if(state == EncodeAlignment) {
              if(m_containsAlignmentInfo) {
                code = m_alignTree->encode(alignPoint);
                if(alignPoint == alignStopSymbol)
                  state = ReadSymbol;
                else
                  state = ReadAlignment;
              }
              else {
                state = ReadSymbol;
                break;
              }
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
          
          //std::cerr << "state : " << state << " " << i << " " << node << std::endl;
          
          if(state == New || state == Phrase)
            node = (byte & mask) ? m_symbolTree->node(node+1) : m_symbolTree->node(node);
          else if(state == Score) {
            size_t treeNum = scores.size() % m_scoreTrees.size();
            node = (byte & mask)
              ? m_scoreTrees[treeNum]->node(node+1)
              : m_scoreTrees[treeNum]->node(node);
          }
          else if(state == Alignment) {
            if(m_containsAlignmentInfo)
              node = (byte & mask) ? m_alignTree->node(node+1) : m_alignTree->node(node);            
          }
          
          //std::cerr << "state : " << state << " " << i << " " << node << std::endl;
          
          if(node < 0) {            
            if(state == New) {
              targetPhrase = new TargetPhrase(Output);
              targetPhrase->SetSourcePhrase(&sourcePhrase);
              
              state = Phrase;
            }
            
            if(state == Phrase) {
              size_t symbol = m_symbolTree->data(-node-1);
              //std::cerr << "Symbol: " << symbol << " " << GetTargetSymbol(symbol) << std::endl;
              if(symbol == phraseStopSymbol)
                state = Score;
              else {
                Word word;
                word.CreateFromString(Output, *m_output, GetTargetSymbol(symbol), false);
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
                if(StaticData::Instance().UseAlignmentInfo())
                  targetPhrase->SetAlignmentInfo(alignment);
                state = Add;
              }
              else {
                std::pair<size_t, size_t>
                alignPointSizeT(alignPoint.first, alignPoint.second);
                alignment.insert(alignPointSizeT);
              }
            }
            
            if(state == Add) {
              //std::cerr << "Adding" << std::endl;
              //std::cerr << targetPhrase << std::endl;
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

  
//  typedef std::pair<float, size_t> Fpair;
//
//struct ScoreSorter {
//  bool operator()(Fpair a, Fpair b) {
//    
//    if(a.second > b.second)
//      return true;
//    
//    if(a.second == b.second && a.first < b.first)
//      return true;
//    
//    return false;
//  }
//};
//
//struct FloatTrans {
//  FloatTrans(boost::unordered_map<float, float> &fm)
//    : m_fm(fm)
//  {}
//  
//  float operator()(float a) {
//    return m_fm[a];
//  }
//  
//  boost::unordered_map<float, float>& m_fm;
//  
//};
  
  //std::cerr << "Creating Huffman tree for " << m_symbolCount.size() << " symbols" << std::endl;
  //m_treeSymbols = new Hufftree<int, size_t>(m_symbolCount.begin(), m_symbolCount.end());
  //
  //{
  //  size_t sum = 0, sumall = 0;
  //  for(SymbolCounter::iterator it = m_symbolCount.begin(); it != m_symbolCount.end(); it++) {
  //    sumall += it->second * m_treeSymbols->encode(it->first).size();
  //    sum    += it->second;
  //  }
  //  std::cerr << double(sumall)/sum << " bits per symbol" << std::endl;
  //}
  //
  //std::vector<Fpair> freqScores;
  //freqScores.insert(freqScores.begin(), scoreCount.begin(), scoreCount.end());
  //std::sort(freqScores.begin(), freqScores.end(), ScoreSorter());
  
  //std::vector<float> topScores;
  //size_t max = 100000;
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
  //
  //std::cerr << "Creating Huffman tree for " << scoreCount.size() << " scores" << std::endl;
  //m_treeScores  = new Hufftree<int, float>(scoreCount.begin(), scoreCount.end());
  //
  //{
  //  size_t sum = 0, sumall = 0;
  //  for(ScoreCounter::iterator it = scoreCount.begin(); it != scoreCount.end(); it++) {
  //    sumall += it->second * m_treeScores->encode(it->first).size();
  //    sum    += it->second;
  //  }
  //  std::cerr << double(sumall)/sum << " bits per score" << std::endl;
  //}
  

}

#endif