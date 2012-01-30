/* Copyright (c) 2011 the authors listed at the following URL, and/or
the authors of referenced articles or incorporated external code:
http://en.literateprograms.org/Huffman_coding_(C_Plus_Plus)?action=history&offset=20090129100015

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Retrieved from: http://en.literateprograms.org/Huffman_coding_(C_Plus_Plus)?oldid=16057
*/

#ifndef HUFFMAN_H__
#define HUFFMAN_H__

#include <vector>
#include <map>

#include <queue>
#include <iostream>
#include <cstdio>

#include <boost/unordered_map.hpp>

template<typename DataType, typename Frequency> class Hufftree
{
public:
  template<typename InputIterator>
  Hufftree(InputIterator begin, InputIterator end);
  Hufftree(std::FILE*);
  ~Hufftree() { delete tree; }

  void Save(std::FILE*);

  std::vector<bool> encode(DataType const& value) {
    return encoding[value];
  }

  template<typename InputIterator>
  std::string encode(InputIterator begin, InputIterator end);
  
  template<typename InputIterator>
  std::string encodeWithLength(InputIterator begin, InputIterator end);

  template<typename InputIterator, typename OutputIterator>
  void decode(DataType length, InputIterator begin, InputIterator end, OutputIterator iter);

  template<typename InputIterator, typename OutputIterator>
  void decodeWithLength(InputIterator begin, InputIterator end, OutputIterator iter);

private:
  class Node;
  
  Node* tree;

  typedef boost::unordered_map<DataType, std::vector<bool> > encodemap;
  encodemap encoding;

  class NodeOrder;
};

template<typename DataType, typename Frequency>
struct Hufftree<DataType, Frequency>::Node
{
  Frequency frequency;
  Node* leftChild;
  static size_t nodes;
  union
  {
    Node* rightChild; // if leftChild != 0
    DataType* data;  // if leftChild == 0
  };

  Node(Frequency f, DataType d):
    frequency(f),
    leftChild(0),
    data(new DataType(d))
  {
    nodes++;
  }

  Node(Node* left, Node* right):
    frequency(left->frequency + right->frequency),
    leftChild(left),
    rightChild(right)
  {
  }

  ~Node()
  {
    if (leftChild)
    {
      delete leftChild;
      delete rightChild;
    }
    else
    {
      delete data;
    }
  }
  
  void Save(std::FILE* out) {
    if (leftChild) {
      leftChild->Save(out);
      rightChild->Save(out);
    }
    else {
      fwrite(&frequency, sizeof(Frequency), 1, out);
      fwrite(data, sizeof(DataType), 1, out);
    }
  }

  void fill(boost::unordered_map<DataType, std::vector<bool> >& encoding,
            std::vector<bool>& prefix)
  {
    if (leftChild)
    {
      prefix.push_back(0);
      leftChild->fill(encoding, prefix);
      prefix.back() = 1;
      rightChild->fill(encoding, prefix);
      prefix.pop_back();
    }
    else
      encoding[*data] = prefix;
  }

};

template<typename DataType, typename Frequency>
template<typename InputIterator>
Hufftree<DataType, Frequency>::Hufftree(InputIterator begin, InputIterator end)
 : tree(0)
{
  std::priority_queue<Node*, std::vector<Node*>, NodeOrder> pqueue;

  while (begin != end) {
    Node* dataNode = new Node(begin->second, begin->first);
    pqueue.push(dataNode);
    ++begin;
  }

  while (!pqueue.empty()) {
    Node* top = pqueue.top();
    pqueue.pop();
    if (pqueue.empty()) {
      tree = top;
    }
    else {
      Node* top2 = pqueue.top();
      pqueue.pop();
      pqueue.push(new Node(top, top2));
    }
  }

  std::vector<bool> bitvec;
  tree->fill(encoding, bitvec);
}

template<typename DataType, typename Frequency>
Hufftree<DataType, Frequency>::Hufftree(std::FILE* in)
 : tree(0)
{
  std::priority_queue<Node*, std::vector<Node*>, NodeOrder> pqueue;

  size_t length;
  fread(&length, sizeof(size_t), 1, in);
  std::cerr << "Nodes: " << length << std::endl;

  size_t count = 0;
  while (count < length) {
    Frequency freq;
    fread(&freq, sizeof(Frequency), 1, in);
    DataType data;
    fread(&data, sizeof(DataType), 1, in);
    
    Node* dataNode = new Node(freq, data);
    pqueue.push(dataNode);
    count++;
  }

  while (!pqueue.empty()) {
    Node* top = pqueue.top();
    pqueue.pop();
    if (pqueue.empty()) {
      tree = top;
    }
    else {
      Node* top2 = pqueue.top();
      pqueue.pop();
      pqueue.push(new Node(top, top2));
    }
  }

  std::vector<bool> bitvec;
  tree->fill(encoding, bitvec);
}

template<typename DataType, typename Frequency>
void Hufftree<DataType, Frequency>::Save(std::FILE* out) {
  size_t length = Node::nodes;
  std::cerr << "Nodes: " << length << std::endl;
  fwrite(&length, sizeof(size_t), 1, out);
  tree->Save(out);
  
  std::cerr << "Nodes2: " << tree->nodes << std::endl;
}

template<typename DataType, typename Frequency>
struct Hufftree<DataType, Frequency>::NodeOrder {
  bool operator()(Node* a, Node* b) {
    if (b->frequency < a->frequency)
      return true;
    if (a->frequency < b->frequency)
      return false;

    if (!a->leftChild && b->leftChild)
      return true;
    if (a->leftChild && !b->leftChild)
      return false;

    if (a->leftChild && b->leftChild) {
      if ((*this)(a->leftChild, b->leftChild))
        return true;
      if ((*this)(b->leftChild, a->leftChild))
        return false;
      return (*this)(a->rightChild, b->rightChild);
    }

    return *(a->data) < *(b->data);
  }
};

//std::ostream& operator<<(std::ostream &os, std::vector<bool> v) {
//  for(std::vector<bool>::iterator it = v.begin(); it != v.end(); it++)
//    os << *it;
//  return os;
//}

template<typename DataType, typename Frequency>
template<typename InputIterator>
std::string Hufftree<DataType, Frequency>::encode(InputIterator begin, InputIterator end) {
  std::string result;
  
  char byte = 0;
  char mask = 1;
  unsigned int pos = 0;
  
  while(begin != end) {    
    std::vector<bool> code = encoding[*begin];
    begin++;
    
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
  }

  if(pos % 8 != 0)
    result.push_back(byte);
  
  return result;
}

template<typename DataType, typename Frequency>
template<typename InputIterator>
std::string Hufftree<DataType, Frequency>::encodeWithLength(InputIterator begin, InputIterator end) {
  std::string result;
  
  char byte = 0;
  char mask = 1;
  unsigned int pos = 0;
  
  size_t lengthNum = std::distance(begin, end);
  DataType length = lengthNum;
  std::vector<bool> code = encoding[length];
  
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

  while(begin != end) {    
    std::vector<bool> code = encoding[*begin];
    begin++;
    
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
  }
  
  if(pos % 8 != 0)
    result.push_back(byte);
  
  return result;
}

template<typename DataType, typename Frequency>
template<typename InputIterator, typename OutputIterator>
void Hufftree<DataType, Frequency>::decode(DataType length, InputIterator begin, InputIterator end, OutputIterator iter) {
  size_t count = 0;
  
  Node* node = tree;
  for(InputIterator it = begin; it != end; it++) {
    char byte = *it;
    char mask = 1;
    
    for(int i = 0; i < 8 && count < length; i++) {
      node = (byte & mask) ? node->rightChild : node->leftChild;
      if(!node->leftChild) {
        *iter++ = *(node->data);
        count++;
        node = tree;
      }
      mask = mask << 1;
    }

    if(count >= length)
      return;
  }
}

template<typename DataType, typename Frequency>
template<typename InputIterator, typename OutputIterator>
void Hufftree<DataType, Frequency>::decodeWithLength(InputIterator begin, InputIterator end, OutputIterator iter) {
  bool has_length = 0;
  DataType length = 0;
  DataType count  = 0;
  
  Node* node = tree;
  for(InputIterator it = begin; it != end; it++) {
    char byte = *it;
    char mask = 1;
    
    int i = 0;
    for(; i < 8 && !has_length; i++) {
      node = (byte & mask) ? node->rightChild : node->leftChild;
      if(!node->leftChild) {
        length = *(node->data);
        node = tree;
        has_length = true;
      }
      mask = mask << 1;
    }

    if(!has_length)
      continue;
    
    for(; i < 8; i++) {
      node = (byte & mask) ? node->rightChild : node->leftChild;
      if(!node->leftChild) {
        *iter++ = *(node->data);
        node = tree;
        
        count++;
        if(count >= length)
          return;
      }
      mask = mask << 1;
    }
  }
}

template<typename DataType, typename Frequency>
size_t Hufftree<DataType, Frequency>::Node::nodes = 0;

#endif
