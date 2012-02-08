#ifndef CMPHSTRINGVECTORADAPTER_H__
#define CMPHSTRINGVECTORADAPTER_H__

#include <cassert>

#include "cmph/src/cmph.h"
#include "StringVector.h"

namespace Moses {
    typedef struct {
        void *vector;
        cmph_uint32 position; 
    } cmph_vector_t;
   
      
    template <typename ValueT, typename PosT, template <typename> class Allocator>
    static cmph_io_adapter_t *CmphStringVectorAdapterNew(StringVector<ValueT, PosT, Allocator>& sv)
    {
        cmph_io_adapter_t * key_source = (cmph_io_adapter_t *)malloc(sizeof(cmph_io_adapter_t));
        cmph_vector_t * cmph_vector = (cmph_vector_t *)malloc(sizeof(cmph_vector_t));
        assert(key_source);
        assert(cmph_vector);
        
        cmph_vector->vector = (void *)&sv;
        cmph_vector->position = 0;
        key_source->data = (void *)cmph_vector;
        key_source->nkeys = sv.size();
        
        return key_source;
    }

    template <typename ValueT, typename PosT, template <typename> class Allocator>
    static int CmphStringVectorAdapterRead(void *data, char **key, cmph_uint32 *keylen) {
        cmph_vector_t *cmph_vector = (cmph_vector_t *)data;
        StringVector<ValueT, PosT, Allocator>* sv = (StringVector<ValueT, PosT, Allocator>*)cmph_vector->vector;
        size_t size;
        *keylen = (*sv)[cmph_vector->position].size();
        size = *keylen;
        *key = new char[size + 1];
        std::string temp = (*sv)[cmph_vector->position];
        strcpy(*key, temp.c_str());
        cmph_vector->position = cmph_vector->position + 1;
        return (int)(*keylen);
    }
    
    static void CmphStringVectorAdapterDispose(void *data, char *key, cmph_uint32 keylen) {
        delete[] key;
    }

    static void CmphStringVectorAdapterRewind(void *data) {
        cmph_vector_t *cmph_vector = (cmph_vector_t *)data;
        cmph_vector->position = 0;
    }

    template <typename ValueT, typename PosT, template <typename> class Allocator>
    static cmph_io_adapter_t* CmphStringVectorAdapter(StringVector<ValueT, PosT, Allocator>& sv) {
        cmph_io_adapter_t * key_source = CmphStringVectorAdapterNew(sv);
        
        key_source->read = CmphStringVectorAdapterRead<ValueT, PosT, Allocator>;
        key_source->dispose = CmphStringVectorAdapterDispose;
        key_source->rewind = CmphStringVectorAdapterRewind;
        return key_source;
    }
    
}

/*

static cmph_io_adapter_t *cmph_io_vector_new(void * vector, cmph_uint32 nkeys)
{
        cmph_io_adapter_t * key_source = (cmph_io_adapter_t *)malloc(sizeof(cmph_io_adapter_t));
        cmph_vector_t * cmph_vector = (cmph_vector_t *)malloc(sizeof(cmph_vector_t));
        assert(key_source);
        assert(cmph_vector);
        cmph_vector->vector = vector;
        cmph_vector->position = 0;
        key_source->data = (void *)cmph_vector;
        key_source->nkeys = nkeys;
        return key_source;
}

static int key_vector_read(void *data, char **key, cmph_uint32 *keylen)
{
        cmph_vector_t *cmph_vector = (cmph_vector_t *)data;
        char **keys_vd = (char **)cmph_vector->vector;
        size_t size;
        *keylen = (cmph_uint32)strlen(keys_vd[cmph_vector->position]);
        size = *keylen;
        *key = (char *)malloc(size + 1);
        strcpy(*key, keys_vd[cmph_vector->position]);
        cmph_vector->position = cmph_vector->position + 1;
        return (int)(*keylen);

}

static void key_vector_dispose(void *data, char *key, cmph_uint32 keylen)
{
        free(key);
}

static void key_vector_rewind(void *data)
{
        cmph_vector_t *cmph_vector = (cmph_vector_t *)data;
        cmph_vector->position = 0;
}

cmph_io_adapter_t *cmph_io_vector_adapter(char ** vector, cmph_uint32 nkeys)
{
        cmph_io_adapter_t * key_source = cmph_io_vector_new(vector, nkeys);
        key_source->read = key_vector_read;
        key_source->dispose = key_vector_dispose;
        key_source->rewind = key_vector_rewind;
        return key_source;
}

void cmph_io_vector_adapter_destroy(cmph_io_adapter_t * key_source)
{
        cmph_io_vector_destroy(key_source);
}

*/

#endif
