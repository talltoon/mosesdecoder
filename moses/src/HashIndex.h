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
#include "CmphStringVectorAdapter.h"

namespace Moses {

class HashIndex {
  private:
    typedef unsigned int Fprint;
    typedef StringVector<unsigned char, size_t, MmapAllocator> SV;
    
    SV m_keys;
    std::vector<Fprint> m_fprints;
    
    CMPH_ALGO m_algo;
    cmph_t* m_hash;
    
    void CalcHashFunction() {
        cmph_io_adapter_t *source = CmphStringVectorAdapter(m_keys);
        //cmph_io_adapter_t *source = cmph_io_vector_adapter((char**) &m_keys[0], m_keys.size());
    
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
        for(SV::string_iterator it = m_keys.begin<SV::string_iterator>();
            it != m_keys.end<SV::string_iterator>(); it++) {
            Fprint fprint = GetFprint(it->c_str());
            size_t idx = cmph_search(m_hash, it->c_str(), (cmph_uint32) it->size());
            
            if(idx >= m_fprints.size())
                m_fprints.resize(idx + 1);
            m_fprints[idx] = fprint;
        }
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
            return idx;
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
        std::fwrite(&m_fprints[0], sizeof(Fprint), nkeys, mphf);
    }
    
    void Load(std::string filename) {
        std::FILE* mphf = std::fopen(filename.c_str(), "r");
        Load(mphf);
    }
    
    void Load(std::FILE * mphf) {
        m_hash = cmph_load(mphf);
        
        size_t nkeys;
        std::fread(&nkeys, sizeof(nkeys), 1, mphf);
        m_fprints.resize(nkeys, 0);
        std::fread(&m_fprints[0], sizeof(Fprint), nkeys, mphf);
    }
    
    size_t GetSize() const {
        return m_fprints.size();
    }
};

}
#endif