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
    typedef boost::unordered_map<unsigned char, size_t> AlignCounter;
    
    SymbolCounter symbolCount;
    ScoreCounter  scoreCount;
    AlignCounter  alignCount; 
  
    StringVector<unsigned char, unsigned, std::allocator> m_targetSymbols;
    std::map<std::string, unsigned> m_targetSymbolsMap;
    
    Hufftree<int, unsigned>* m_symbolTree;
    Hufftree<int, float>* m_scoreTree;
    
    // ***********************************************
    
    const std::vector<FactorType>* m_output;
    const PhraseDictionaryFeature* m_feature;
    size_t m_numScoreComponent;
    const std::vector<float>* m_weight;
    float m_weightWP;
    const LMList* m_languageModels;
  
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
    //unsigned char c = (unsigned char) words.size() + 1;
    //os.write((char*) &c, 1);  
    
    unsigned stpIdx = AddOrGetTargetSymbol("__SPECIAL_STOP_SYMBOL__");
    for(size_t i = 0; i < words.size(); i++) {
      unsigned idx = AddOrGetTargetSymbol(words[i]);
      os.write((char*)&idx, sizeof(idx));
      symbolCount[idx]++;
    }
    os.write((char*)&stpIdx, sizeof(stpIdx));
    symbolCount[stpIdx]++;
  }
  
  void packScores(std::string scores, std::ostream& os) {
    std::stringstream ss(scores);
    float score;
    while(ss >> score) {
      score = FloorScore(TransformScore(score));
      os.write((char*)&score, sizeof(score));
      scoreCount[score]++;
    }
  }
  
  void packAlignment(std::string alignment, std::ostream& os) {
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
    : m_output(output), m_feature(feature), m_numScoreComponent(numScoreComponent),
      m_weight(weight), m_weightWP(weightWP), m_languageModels(languageModels),
      m_symbolTree(0), m_scoreTree(0)
    { }
    
    ~PhraseCoder() {
      if(m_symbolTree)
        delete m_symbolTree;
      if(m_scoreTree)
        delete m_scoreTree;
    }
    
    void load(std::FILE* in) {
      m_targetSymbols.load(in);
      m_symbolTree = new Hufftree<int, unsigned>(in);
      m_scoreTree = new Hufftree<int, float>(in);
    }
    
    void save(std::FILE* out) {
      m_targetSymbols.save(out);
      m_symbolTree->Save(out);
      m_scoreTree->Save(out);
    }
    
    void calcHuffmanCodes() {
      std::cerr << "Creating Huffman codes for " << symbolCount.size() << " symbols" << std::endl;
      m_symbolTree = new Hufftree<int, unsigned>(symbolCount.begin(), symbolCount.end());      
      {
        size_t sum = 0, sumall = 0;
        for(SymbolCounter::iterator it = symbolCount.begin(); it != symbolCount.end(); it++) {
          sumall += it->second * m_symbolTree->encode(it->first).size();
          sum    += it->second;
        }
        std::cerr << double(sumall)/sum << " bits per symbol" << std::endl;
      }
      
      std::cerr << "Creating Huffman codes for " << scoreCount.size() << " scores" << std::endl;
      m_scoreTree = new Hufftree<int, float>(scoreCount.begin(), scoreCount.end());
      {  
        size_t sum = 0, sumall = 0;
        for(ScoreCounter::iterator it = scoreCount.begin(); it != scoreCount.end(); it++) {
          sumall += it->second * m_scoreTree->encode(it->first).size();
          sum    += it->second;
        }
        std::cerr << double(sumall)/sum << " bits per score" << std::endl;
      }
    }
    
    std::string packCollection(StringCollection collection) {
      std::stringstream packedCollectionStream;
      packedCollectionStream.unsetf(std::ios::skipws);
      
      for(StringCollection::iterator it = collection.begin(); it != collection.end(); it++) {
        packTargetPhrase((*it)[1], packedCollectionStream);
        packScores((*it)[2], packedCollectionStream);
        //packAlignment((*it)[3], packedCollectionStream);        
      }
      
      return packedCollectionStream.str();
    }
    
    std::string encodePackedCollection(std::string packedCollection) {
      
      enum EncodeState {
        ReadSymbol, ReadScore, ReadAlignment,
        EncodeSymbol, EncodeScore, EncodeAlignment };
      EncodeState state = ReadSymbol;
      
      unsigned phraseStopSymbol = m_targetSymbolsMap["__SPECIAL_STOP_SYMBOL__"];
      
      std::stringstream packedStream(packedCollection);
      packedStream.unsetf(std::ios::skipws);
      std::string result;
      
      char byte = 0;
      char mask = 1;
      unsigned int pos = 0;
      
      unsigned symbol;
      float score;
      size_t currScore = 0;
      
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
              currScore++;
              packedStream.read((char*) &score, sizeof(float));
              state = EncodeScore;
            }
            break;
          case ReadAlignment:
            // ..
            state = EncodeAlignment;
            break;
          case EncodeSymbol:
          case EncodeScore:
          case EncodeAlignment:
            std::vector<bool> code;
            if(state == EncodeSymbol) {
              //std::cerr << symbol << " " << GetTargetSymbol(symbol) << std::endl;
              code = m_symbolTree->encode(symbol);
              if(symbol == phraseStopSymbol)
                state = ReadScore;
              else
                state = ReadSymbol;
            }
            else if(state == EncodeScore) {
              //std::cerr << score << std::endl;
              code = m_scoreTree->encode(score);
              state = ReadScore;
            }
            else {
              // ...
              code = std::vector<bool>();
              state = ReadSymbol;
              break;
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
      
      TargetPhrase* targetPhrase;
      std::vector<float> scores;
        
      enum DecodeState { New, Phrase, Score, Alignment, Add } state = New;
      
      int node = 0;
      
      for(std::string::iterator it = encoded.begin(); it != encoded.end(); it++) {
        char byte = *it;
        char mask = 1;
        
        for(int i = 0; i < 8; i++) {
          
          //std::cerr << "state : " << state << " " << i << " " << node << std::endl;
          
          if(state == New || state == Phrase)
            node = (byte & mask) ? m_symbolTree->node(node+1) : m_symbolTree->node(node);
          else if(state == Score)
            node = (byte & mask) ? m_scoreTree->node(node+1) : m_scoreTree->node(node);
          else if(state == Alignment) {
            //...
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
              float score = m_scoreTree->data(-node-1);
              scores.push_back(score);
              if(scores.size() == m_numScoreComponent) {
                targetPhrase->SetScore(m_feature, scores, *m_weight, m_weightWP, *m_languageModels);
                scores.clear();
                
                state = Add;
              }
            }
            else if(state == Alignment) {
              // ...
              state = Add;
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
  
  //std::cerr << "Creating Huffman tree for " << symbolCount.size() << " symbols" << std::endl;
  //m_treeSymbols = new Hufftree<int, size_t>(symbolCount.begin(), symbolCount.end());
  //
  //{
  //  size_t sum = 0, sumall = 0;
  //  for(SymbolCounter::iterator it = symbolCount.begin(); it != symbolCount.end(); it++) {
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