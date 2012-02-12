#ifndef HASHINDEX_H__
#define HASHINDEX_H__

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

#include "cmph/src/cmph.h"
#include "MurmurHash3.h"
#include "uint8n.h"
#include "StringVector.h"
#include "MmapAllocator.h"
#include "CmphStringVectorAdapter.h"

namespace Moses {

template <template <typename> class Allocator1 = std::allocator,
          template <typename> class Allocator2 = MmapAllocator>
class HashIndex {
  private:
    typedef unsigned int Fprint;
    typedef unsigned int PosType;
    
    std::vector<Fprint, Allocator1<Fprint> > m_fprints;
    std::vector<PosType, Allocator1<PosType> > m_posMap;
    
    typedef StringVector<unsigned char, size_t, Allocator2> SV;
    SV m_keys;

    
    CMPH_ALGO m_algo;
    cmph_t* m_hash;
    
    void CalcHashFunction() {
        cmph_io_adapter_t *source = CmphStringVectorAdapter(m_keys);
    
        cmph_config_t *config = cmph_config_new(source);
        cmph_config_set_algo(config, m_algo);
        cmph_config_set_verbosity(config, 5);
        
        m_hash = cmph_new(config);
        cmph_config_destroy(config);
    }
    
    Fprint GetFprint(const char* key) const {
        Fprint hash;
        MurmurHash3_x86_32(key, std::strlen(key), 100000, &hash);
        return hash;
    }
    
    void CalcFprints() {
        std::cerr << "Calculating finger prints for source phrases and mapping" << std::endl;
        m_fprints.resize(m_keys.size());
        m_posMap.resize(m_keys.size());
        
        size_t i = 0;
        for(typename SV::iterator it = m_keys.begin(); it != m_keys.end(); it++) {
            if((i+1) % 100000 == 0)
              std::cerr << ".";
            if((i+1) % 5000000 == 0)
              std::cerr << "[" << (i+1) << "]" << std::endl;  

            std::string temp = *it;
            Fprint fprint = GetFprint(temp.c_str());
            size_t idx = cmph_search(m_hash, temp.c_str(), (cmph_uint32) temp.size());
            
            m_fprints[idx] = fprint;
            m_posMap[idx] = i;
            i++;
        }
        std::cerr << std::endl;
    }  
    
  public:
    HashIndex() : m_algo(CMPH_CHD) {}
    HashIndex(CMPH_ALGO algo) : m_algo(algo) {}
    
    ~HashIndex() {
        cmph_destroy(m_hash);        
        ClearKeys();
    }
    
    size_t GetHash(const char* key) const {
        size_t idx = cmph_search(m_hash, key, (cmph_uint32) strlen(key));
        if(GetFprint(key) == m_fprints[idx])
            return m_posMap[idx];
        else
            return GetSize();
    }
    
    size_t GetHash(std::string key) const {
        return GetHash(key.c_str());
    }
    
    size_t operator[](std::string key) const {
        return GetHash(key);
    }

    size_t operator[](char* key) const {
        return GetHash(key);
    }
    
    size_t GetMapPos(size_t index) const {
      if(m_posMap.size() > index)
        return m_posMap[index];
      return GetSize();
    }
    
    size_t GetHashByIndex(size_t index) const {
        if(index < m_keys.size())
            return GetHash(m_keys[index]);
        return GetSize();
    }
    
    void AddKey(const char* keyArg) {
        AddKey(std::string(keyArg));
    }
    
    void AddKey(std::string keyArg) {
        m_keys.push_back(keyArg);
    }
   
    void ClearKeys() {
      SV empty;
      empty.swap(m_keys);
    }
    
    void Create() {
        CalcHashFunction();
        CalcFprints();
    }
    
    void Save(std::string filename) {
        std::FILE* mphf = std::fopen(filename.c_str(), "w");
        Save(mphf);
    }
    
    void Save(std::FILE * mphf) {
        cmph_dump(m_hash, mphf);
        
        size_t nkeys = m_fprints.size();
        std::fwrite(&nkeys, sizeof(nkeys), 1, mphf);
        std::fwrite(&m_posMap[0], sizeof(PosType), nkeys, mphf);
        std::fwrite(&m_fprints[0], sizeof(Fprint), nkeys, mphf);
    }
    
    void Load(std::string filename) {
        std::FILE* mphf = std::fopen(filename.c_str(), "r");
        Load(mphf);
    }
    
    void Load(std::FILE * mphf) {
        size_t a1 = std::ftell(mphf);
        m_hash = cmph_load(mphf);
        size_t a2 = std::ftell(mphf);
        
        std::cerr << "CMPH size: " << float(a2 - a1)/(1024*1024) << " Mb" << std::endl;
        
        size_t nkeys;
        std::fread(&nkeys, sizeof(nkeys), 1, mphf);
        m_fprints.resize(nkeys, 0);
        m_posMap.resize(nkeys, 0);

        std::fread(&m_posMap[0], sizeof(PosType), nkeys, mphf);
        size_t a3 = std::ftell(mphf);
        std::cerr << "PosMap size: " << float(a3 - a2)/(1024*1024) << " Mb" << std::endl;

        std::fread(&m_fprints[0], sizeof(Fprint), nkeys, mphf);
        size_t a4 = std::ftell(mphf);
        std::cerr << "Fprints size: " << float(a4 - a3)/(1024*1024) << " Mb" << std::endl;

        std::cerr << "Total HashIndex size: " << float(a4 - a1)/(1024*1024) << " Mb" << std::endl;

    }
    
    size_t GetSize() const {
        return m_fprints.size();
    }
};

}
#endif