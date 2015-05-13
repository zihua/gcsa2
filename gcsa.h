/*
  Copyright (c) 2015 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#ifndef _GCSA_GCSA_H
#define _GCSA_GCSA_H

#include "support.h"

namespace gcsa
{

//------------------------------------------------------------------------------

class GCSA
{
public:
  typedef gcsa::size_type  size_type;
  typedef sdsl::wt_huff<>  bwt_type;
  typedef sdsl::bit_vector bit_vector;

//------------------------------------------------------------------------------

  GCSA();
  GCSA(const GCSA& g);
  GCSA(GCSA&& g);
  ~GCSA();

  void swap(GCSA& g);
  GCSA& operator=(const GCSA& g);
  GCSA& operator=(GCSA&& g);

  size_type serialize(std::ostream& out, sdsl::structure_tree_node* v = nullptr, std::string name = "") const;
  void load(std::istream& in);

  const static std::string EXTENSION;  // .gcsa

  const static size_type DOUBLING_STEPS = 3;

  const static char_type ENDMARKER = '$';
  const static comp_type ENDMARKER_COMP = 0;
  const static char_type SOURCE_MARKER = '#';

//------------------------------------------------------------------------------

  /*
    The input vector must be sorted and contain only unique kmers of length 16 or less.
    Each kmer must have at least one predecessor and one successor.
  */
  GCSA(const std::vector<key_type>& keys, size_type kmer_length, const Alphabet& _alpha = Alphabet());

  /*
    This is the main constructor. We build GCSA from the kmers, doubling the path length
    three times. The kmer array will be cleared during the construction.

    FIXME option to change the number of doubling steps
  */
  GCSA(std::vector<KMer>& kmers, size_type kmer_length, const Alphabet& _alpha = Alphabet());

//------------------------------------------------------------------------------

  /*
    The high-level interface deals with path ranges and actual character values.
    locate() stores the node identifiers in the given vector in sorted order.
    If append is true, the results are appended to the existing vector.
    If sort is true, the results are sorted and the duplicates are removed.

    The implementation of find() is based on random access iterators. Bidirectional
    iterators would be enough without the query length check.
  */

  template<class Iterator>
  range_type find(Iterator begin, Iterator end) const
  {
    if(begin == end) { return range_type(0, this->size() - 1); }
    if((size_type)(end - begin) > this->order())
    {
      std::cerr << "GCSA::find(): Query length exceeds " << this->order() << std::endl;
      return range_type(1, 0);
    }

    --end;
    range_type range = this->pathNodeRange(gcsa::charRange(this->alpha, this->alpha.char2comp[*end]));
    while(!Range::empty(range) && end != begin)
    {
      --end;
      range = this->LF(range, this->alpha.char2comp[*end]);
    }

    return range;
  }

  template<class Container>
  range_type find(const Container& pattern) const
  {
    return this->find(pattern.begin(), pattern.end());
  }

  range_type find(const char_type* pattern, size_type length) const;

  void locate(size_type path, std::vector<node_type>& results, bool append = false, bool sort = true) const;
  void locate(range_type range, std::vector<node_type>& results, bool append = false, bool sort = true) const;

//------------------------------------------------------------------------------

  /*
    The low-level interface deals with paths, path ranges, and contiguous character values.
  */

  inline size_type size() const { return this->path_node_count; }
  inline size_type edge_count() const { return this->bwt.size(); }
  inline size_type order() const { return this->max_query_length; }

  inline bool has_samples() const { return (this->stored_samples.size() > 0); }
  inline size_type sample_count() const { return this->stored_samples.size(); }
  inline size_type sample_bits() const { return this->stored_samples.width(); }

  inline range_type charRange(comp_type comp) const
  {
    return this->pathNodeRange(gcsa::charRange(this->alpha, comp));
  }

  inline size_type LF(size_type path_node, comp_type comp) const
  {
    path_node = this->startPos(path_node);
    path_node = gcsa::LF(this->bwt, this->alpha, path_node, comp);
    return this->edge_rank(path_node);
  }

  inline range_type LF(range_type range, comp_type comp) const
  {
    range = this->bwtRange(range);
    range = gcsa::LF(this->bwt, this->alpha, range, comp);
    if(Range::empty(range)) { return range; }
    return this->pathNodeRange(range);
  }

  // Follow the first edge backwards.
  inline size_type LF(size_type path_node) const
  {
    path_node = this->startPos(path_node);
    auto temp = this->bwt.inverse_select(path_node);
    path_node = this->alpha.C[temp.second] + temp.first;
    return this->edge_rank(path_node);
  }

//------------------------------------------------------------------------------

  /*
    We could have a bitvector for mapping between the node identifiers returned by locate()
    and the original (node, offset) pairs. Alternatively, if the maximum length of a label
    is reasonable, we can just use (node, offset) = (id / max_length, id % max_length).
  */

  size_type                 path_node_count;
  size_type                 max_query_length;

  bwt_type                  bwt;
  Alphabet                  alpha;

  // The last BWT position in each path is marked with an 1-bit.
  bit_vector                path_nodes;
  bit_vector::rank_1_type   path_rank;
  bit_vector::select_1_type path_select;

  // The last outgoing edge from each path is marked with an 1-bit.
  bit_vector                edges;
  bit_vector::rank_1_type   edge_rank;
  bit_vector::select_1_type edge_select;

  // Paths containing samples are marked with an 1-bit.
  bit_vector                sampled_paths;
  bit_vector::rank_1_type   sampled_path_rank;

  // The last sample belonging to the same path is marked with an 1-bit.
  sdsl::int_vector<0>       stored_samples;
  bit_vector                samples;
  bit_vector::select_1_type sample_select;

//------------------------------------------------------------------------------

private:
  void copy(const GCSA& g);
  void setVectors();

  /*
    Increases path length to up to 2^DOUBLING_STEPS times the original and returns
    the actual path length multiplier. Sets max_query_length.

    Vector last_labels will contain the lexicographically largest labels of merged
    nodes.
  */
  size_type prefixDoubling(std::vector<PathNode>& paths, size_type kmer_length,
    std::vector<PathNode>& last_labels);

  /*
    Merges path nodes having the same label. Writes the additional from nodes to the given
    vector as pairs (path rank, node). Sets path_node_count.
  */
  void mergeByLabel(std::vector<PathNode>& paths, size_type path_order, std::vector<range_type>& from_nodes,
    std::vector<PathNode>& last_labels);

  /*
    Store the number of outgoing edges in the to fields of each node.
  */
  size_type countEdges(std::vector<PathNode>& paths, size_type path_order,
    std::vector<PathNode>& last_labels,
    const GCSA& mapper, const sdsl::int_vector<0>& last_char);

  /*
    Builds the structures related to samples. Clears from_nodes.

    We sample a path node, if it a) has multiple predecessors; b) is the source node;
    c) has offset 0 for at least one from node; or d) the set of node ids is different
    from the predecessor, or the offset is anything other than the offset in the
    predecessor (for the same id) +1.

    FIXME Later: An alternate sampling scheme for graphs where nodes are not (id, offset)
    pairs.
  */
  void sample(std::vector<PathNode>& paths, std::vector<range_type>& from_nodes);

  void locateInternal(size_type path, std::vector<node_type>& results) const;

//------------------------------------------------------------------------------

  struct KeyGetter
  {
    inline static size_type predecessors(key_type key) { return Key::predecessors(key); }
    inline static size_type outdegree(key_type key)
    {
      return sdsl::bits::lt_cnt[Key::successors(key)];
    }
  };

  struct PathNodeGetter
  {
    inline static size_type predecessors(const PathNode& path) { return path.predecessors(); }
    inline static size_type outdegree(const PathNode& path) { return path.outdegree(); }
  };

  template<class NodeType, class Getter>
  void build(const std::vector<NodeType>& nodes, const Alphabet& _alpha, size_type total_edges)
  {
    sdsl::int_vector<64> counts(_alpha.sigma, 0);
    sdsl::int_vector<8> buffer(total_edges, 0);
    this->path_nodes = bit_vector(total_edges, 0);
    this->edges = bit_vector(total_edges, 0);
    for(size_type i = 0, bwt_pos = 0, edge_pos = 0; i < nodes.size(); i++)
    {
      size_type pred = Getter::predecessors(nodes[i]);
      for(size_type j = 0; j < _alpha.sigma; j++)
      {
        if(pred & (((size_type)1) << j))
        {
          buffer[bwt_pos] = j; bwt_pos++;
          counts[j]++;
        }
      }
      this->path_nodes[bwt_pos - 1] = 1;
      edge_pos += Getter::outdegree(nodes[i]);
      this->edges[edge_pos - 1] = 1;
    }
    directConstruct(this->bwt, buffer);
    this->alpha = Alphabet(counts, _alpha.char2comp, _alpha.comp2char);

    sdsl::util::init_support(this->path_rank, &(this->path_nodes));
    sdsl::util::init_support(this->path_select, &(this->path_nodes));
    sdsl::util::init_support(this->edge_rank, &(this->edges));
    sdsl::util::init_support(this->edge_select, &(this->edges));
  }

//------------------------------------------------------------------------------

  inline size_type startPos(size_type path_node) const
  {
    return (path_node > 0 ? this->path_select(path_node) + 1 : 0);
  }

  inline size_type endPos(size_type path_node) const
  {
    return this->path_select(path_node + 1);
  }

  inline range_type bwtRange(range_type path_node_range) const
  {
    path_node_range.first = this->startPos(path_node_range.first);
    path_node_range.second = this->endPos(path_node_range.second);
    return path_node_range;
  }

  inline range_type pathNodeRange(range_type incoming_range) const
  {
    incoming_range.first = this->edge_rank(incoming_range.first);
    incoming_range.second = this->edge_rank(incoming_range.second);
    return incoming_range;
  }

  inline range_type sampleRange(size_type path_node) const
  {
    path_node = this->sampled_path_rank(path_node);
    range_type sample_range;
    sample_range.first = (path_node > 0 ? this->sample_select(path_node) + 1 : 0);
    sample_range.second = this->sample_select(path_node + 1);
    return sample_range;
  }
};  // class GCSA

//------------------------------------------------------------------------------

} // namespace gcsa

#endif // _GCSA_GCSA_H
