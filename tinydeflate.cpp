// tinydeflate.cpp v1.01 - C++ class for DEFLATE/zlib compression (implements RFC 1950 and RFC 1951).
// Public domain, Rich Geldreich <richgel99@gmail.com>, last updated April 24, 2011
// Also includes a few misc. goodies:
// Simple PNG writer function by Alex Evans, 2011. Released into the public domain: https://gist.github.com/908299, more context at http://altdevblogaday.org/2011/04/06/a-smaller-jpg-encoder/.
// Karl Malbrain's compact CRC-32. See "A compact CCITT crc16 and crc32 C implementation that balances processor cache usage against speed": http://www.geocities.com/malbrain/
//
// Compressor limitations: Only supports dynamic blocks, so it may slightly expand already compressed data.
// Also, it's currently not smart about how it breaks up the stream into separate dynamic blocks.
//
// This is an stb_image.c-like header file library. If you only want the header, define TINYDEFLATE_HEADER_FILE_ONLY before including this file.
#ifndef TINYDEFLATE_HEADER_INCLUDED
#define TINYDEFLATE_HEADER_INCLUDED

namespace tinydeflate
{
  typedef unsigned char uint8; typedef signed short int16; typedef unsigned short uint16; typedef unsigned int uint32; typedef unsigned int uint;

  // Compression parameters/flags (logically OR together):
  // DEFAULT_MAX_PROBES: The compressor defaults to 100 dictionary probes per dictionary search: 0=fastest (Huffman only), 1=fastest (Huffman+LZ), 4095=slowest.
  // NONDETERMINISTIC_PARSING_FLAG: Enable to decrease the compressor's initialization time to the minimum, but the output may vary from run to run given the same input (depending on the contents of memory).
  // GREEDY_PARSING_FLAG: Set to use faster greedy parsing, instead of more efficient lazy parsing.
  // WRITE_ZLIB_HEADER: If set, the compressor outputs a zlib header before the deflate data, and the Adler-32 of the source data at the end. Otherwise, you'll get raw deflate data.
  enum { DEFAULT_MAX_PROBES = 100, NONDETERMINISTIC_PARSING_FLAG = 0x20000000, GREEDY_PARSING_FLAG = 0x40000000, WRITE_ZLIB_HEADER = 0x80000000 };

  // High level compression functions:
  // compress_mem_to_heap() compresses a block in memory to a heap block allocated via malloc().
  // On entry:
  //  pSrc_buf, src_buf_len: Pointer and size of source block to compress.
  //  flags: The max match finder probes (default is 100) logically OR'd against the above flags. Higher probes are slower but improve compression.
  // On return: 
  //  Function returns a pointer to the compressed data, or NULL on failure. 
  //  *pOut_len will be set to the compressed data's size, which could be larger than src_buf_len on uncompressible data.
  //  The caller must free() the returned block when it's no longer needed.
  void *compress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len, size_t *pOut_len, int flags = DEFAULT_MAX_PROBES | WRITE_ZLIB_HEADER);
  
  // compress_mem_to_mem() compresses a block in memory to another block in memory. 
  // Returns 0 on failure.
  size_t compress_mem_to_mem(void *pOut_buf, size_t out_buf_len, const void *pSrc_buf, size_t src_buf_len, int flags = DEFAULT_MAX_PROBES | WRITE_ZLIB_HEADER);
  
  // Compresses an image to a compressed PNG file in memory.
  // On entry:
  //  pImage, w, h, and num_chans describe the image to compress. num_chans may be 1, 2, 3, or 4.
  // On return:
  //  Function returns a pointer to the compressed data, or NULL on failure.
  //  *pLen_out will be set to the size of the PNG image file.
  //  The caller must free() the returned heap block (which will typically be larger than *pLen_out) when it's no longer needed.
  void *write_image_to_png_file_in_memory(const void *pImage, int w, int h, int num_chans, uint32 *pLen_out);

  // Output stream interface. The compressor uses this interface to write compressed data. It'll typically be called OUT_BUF_SIZE (4KB) at a time.
  class output_stream
  {
  public:
    virtual ~output_stream() { };
    virtual bool put_buf(const void* pBuf, int len) = 0;
  };

  // compress_mem_to_output_stream() compresses a block to an output stream. The above helpers use this function internally.
  bool compress_mem_to_output_stream(const void *pBuf, size_t buf_len, output_stream *pStream, int flags = DEFAULT_MAX_PROBES | WRITE_ZLIB_HEADER);

  // Auto-resizing heap output stream class.
  class expandable_malloc_output_stream : public output_stream
  {
    size_t m_size, m_capacity;
    void *m_pBuf;
  public:
    inline expandable_malloc_output_stream::expandable_malloc_output_stream(size_t initial_capacity = 0) : m_pBuf(0), m_size(0), m_capacity(0) { init(initial_capacity); }
    virtual ~expandable_malloc_output_stream() { clear(); }

    void init(size_t initial_capacity);
    void clear();
    inline const uint8 *get_buf() const { return static_cast<const uint8*>(m_pBuf); }
    inline uint8 *get_buf() { return static_cast<uint8*>(m_pBuf); }
    inline size_t get_size() { return m_size; }
    inline size_t get_capacity() { return m_capacity; }
    inline void *assume_buf_ownership() { void *p = m_pBuf; m_pBuf = 0; m_size = m_capacity = 0; return p; }

    virtual bool put_buf(const void* pBuf, int len);
  };

  // Memory block output stream class.
  class buffer_output_stream : public output_stream
  {
    size_t m_size, m_capacity;
    void *m_pBuf;
  public:
    inline buffer_output_stream(void *pBuf = 0, size_t buf_size = 0) { init(pBuf, buf_size); }
    virtual ~buffer_output_stream() { clear(); }

    inline void init(void *pBuf, size_t buf_size) { m_pBuf = pBuf, m_size = 0; m_capacity = buf_size; }
    inline void clear() { m_pBuf = 0; m_size = m_capacity = 0; }
    inline const uint8 *get_buf() const { return static_cast<const uint8*>(m_pBuf); }
    inline uint8 *get_buf() { return static_cast<uint8*>(m_pBuf); }
    inline size_t get_size() { return m_size; }
    inline size_t get_capacity() { return m_capacity; }

    virtual bool put_buf(const void* pBuf, int len);
  };

  uint32 adler32(const uint8 *ptr, size_t buf_len, uint32 adler32 = 0);
  uint32 crc32(const uint8 *ptr, size_t buf_len, uint32 crc = 0);

  // This class may be used directly if the above helper functions aren't flexible enough. This class does not make any heap allocations, unlike the above helper functions.
  class compressor
  {
  public:
    compressor() : m_pStream(0), m_all_writes_succeeded(false) { }

    // Initializes the compressor.
    bool init(output_stream *pStream, int flags = DEFAULT_MAX_PROBES | WRITE_ZLIB_HEADER);
    
    // Compresses a block of data. 
    // To flush the compressor: call this function with pData set to NULL and data_len set to 0. This function cannot be called again once this is done, but you can call init() to reinitialize to compress again.
    bool compress_data(const void *pData, uint data_len);

    inline bool get_all_writes_succeeded() const { return m_all_writes_succeeded; }

  private:
    enum 
    { 
      OUT_BUF_SIZE = 4096, MAX_HUFF_TABLES = 3, MAX_HUFF_SYMBOLS = 384, MAX_HUFF_SYMBOLS_0 = 288, MAX_HUFF_SYMBOLS_1 = 32, MAX_HUFF_SYMBOLS_2 = 19,
      LZ_DICT_SIZE = 32768, LZ_DICT_SIZE_MASK = LZ_DICT_SIZE - 1, MIN_MATCH_LEN = 3, MAX_MATCH_LEN = 258, LZ_HASH_BITS = 12, LZ_HASH_SIZE = 1 << LZ_HASH_BITS, LZ_CODE_BUF_SIZE = 24U * 1024U,
    };

    output_stream *m_pStream;
    uint m_flags, m_max_probes; 
    bool m_greedy_parsing, m_all_writes_succeeded;
    uint m_adler32, m_lookahead_pos, m_lookahead_size, m_dict_size;
    uint8 *m_pLZ_code_buf, *m_pLZ_flags, *m_pOutput_buf;
    uint m_num_flags_left, m_bits_in, m_bit_buffer;
    uint m_saved_match_dist, m_saved_match_len, m_saved_lit;
    uint8 m_dict[LZ_DICT_SIZE + MAX_MATCH_LEN - 1];
    uint16 m_huff_count[MAX_HUFF_TABLES][MAX_HUFF_SYMBOLS];
    uint16 m_huff_codes[MAX_HUFF_TABLES][MAX_HUFF_SYMBOLS];
    uint8 m_huff_code_sizes[MAX_HUFF_TABLES][MAX_HUFF_SYMBOLS];
    uint8 m_lz_code_buf[LZ_CODE_BUF_SIZE];
    uint16 m_next[LZ_DICT_SIZE];
    uint16 m_hash[LZ_HASH_SIZE];
    uint8 m_output_buf[OUT_BUF_SIZE];

    void optimize_huffman_table(int table_num, int table_len, int code_size_limit);
    inline void flush_output_buffer();
    void start_dynamic_block(bool last_block);
    void flush_block(bool last_block);
    inline void record_literal(uint8 lit);
    inline void record_match(uint match_len, uint match_dist);
    inline void find_match(uint pos, uint max_dist, uint max_match_len, uint &match_dist, uint &match_len);
  };

} // tinydeflate

#endif // TINYDEFLATE_HEADER_INCLUDED

// -------------------  End of header file
// If you only want the header file, define TINYDEFLATE_HEADER_FILE_ONLY then include this file (tinydeflate.cpp).

#ifndef TINYDEFLATE_HEADER_FILE_ONLY

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define TDEFL_ASSERT(x) assert(x)

// The core tinydeflate::compressor class doesn't use the heap at all, but the optional high-level helper functions do.
#define TDEFL_MALLOC(x) malloc(x)
#define TDEFL_FREE(x) free(x)
#define TDEFL_REALLOC(p, x) realloc(p, x)
#define TDEFL_NEW new
#define TDEFL_DELETE delete

#define TDEFL_MAX(a,b) (((a)>(b))?(a):(b))
#define TDEFL_MIN(a,b) (((a)<(b))?(a):(b))

namespace tinydeflate
{
  // Purposely making these tables static for faster init and thread safety.
  static const uint16 s_len_sym[256] = {
    257,258,259,260,261,262,263,264,265,265,266,266,267,267,268,268,269,269,269,269,270,270,270,270,271,271,271,271,272,272,272,272,
    273,273,273,273,273,273,273,273,274,274,274,274,274,274,274,274,275,275,275,275,275,275,275,275,276,276,276,276,276,276,276,276,
    277,277,277,277,277,277,277,277,277,277,277,277,277,277,277,277,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,278,
    279,279,279,279,279,279,279,279,279,279,279,279,279,279,279,279,280,280,280,280,280,280,280,280,280,280,280,280,280,280,280,280,
    281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,281,
    282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,282,
    283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,283,
    284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,284,285 };

  static const uint8 s_len_extra[256] = {
    0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,0 };

  static const uint8 s_small_dist_sym[512] = {
    0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,11,11,11,11,11,11,
    11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,16,16,16,16,16,16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
    17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17 };

  static const uint8 s_small_dist_extra[512] = {
    0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7 };

  static const uint8 s_large_dist_sym[128] = {
    0,0,18,19,20,20,21,21,22,22,22,22,23,23,23,23,24,24,24,24,24,24,24,24,25,25,25,25,25,25,25,25,26,26,26,26,26,26,26,26,26,26,26,26,
    26,26,26,26,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,28,
    28,28,28,28,28,28,28,28,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29 };

  static const uint8 s_large_dist_extra[128] = {
    0,0,8,8,9,9,9,9,10,10,10,10,10,10,10,10,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
    12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13 };
  
  template <class T> inline void clear_obj(T &obj) { memset(&obj, 0, sizeof(obj)); }

  uint32 adler32(const uint8 *ptr, size_t buf_len, uint32 adler32)
  {
    uint32 i, s1 = adler32 & 0xffff, s2 = adler32 >> 16; size_t block_len = buf_len % 5552;
    while (buf_len) {
      for (i = 0; i + 7 < block_len; i += 8, ptr += 8) {
        s1 += ptr[0], s2 += s1; s1 += ptr[1], s2 += s1; s1 += ptr[2], s2 += s1; s1 += ptr[3], s2 += s1; 
        s1 += ptr[4], s2 += s1; s1 += ptr[5], s2 += s1; s1 += ptr[6], s2 += s1; s1 += ptr[7], s2 += s1; 
      }
      for ( ; i < block_len; ++i) s1 += *ptr++, s2 += s1;
      s1 %= 65521U, s2 %= 65521U; buf_len -= block_len; block_len = 5552;
    }
    return (s2 << 16) + s1;
  }

  uint32 crc32(const uint8 *ptr, size_t buf_len, uint32 crc)
  {
    static const uint32 s_crc32[16] = { 0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
    crc = ~crc; while (buf_len--) { uint8 b = *ptr++; crc = (crc >> 4) ^ s_crc32[(crc & 0xF) ^ (b & 0xF)]; crc = (crc >> 4) ^ s_crc32[(crc & 0xF) ^ (b >> 4)]; } return ~crc;
  }
  
  // Radix sorts sym_freq[] array by 16-bit key m_key. Returns ptr to sorted values.
  struct sym_freq { uint16 m_key, m_sym_index; };
  static inline sym_freq* radix_sort_syms(uint num_syms, sym_freq* pSyms0, sym_freq* pSyms1)
  {
    const uint cMaxPasses = 2; uint32 hist[256 * cMaxPasses]; clear_obj(hist);
    for (uint i = 0; i < num_syms; i++) { uint freq = pSyms0[i].m_key; hist[freq & 0xFF]++; hist[256 + ((freq >> 8) & 0xFF)]++; }
    sym_freq* pCur_syms = pSyms0, *pNew_syms = pSyms1;
    uint total_passes = cMaxPasses; while ((total_passes > 1) && (num_syms == hist[(total_passes - 1) * 256])) total_passes--;
    for (uint pass_shift = 0, pass = 0; pass < total_passes; pass++, pass_shift += 8)
    {
      const uint32* pHist = &hist[pass << 8];
      uint offsets[256], cur_ofs = 0;
      for (uint i = 0; i < 256; i++) { offsets[i] = cur_ofs; cur_ofs += pHist[i]; }
      for (uint i = 0; i < num_syms; i++) pNew_syms[offsets[(pCur_syms[i].m_key >> pass_shift) & 0xFF]++] = pCur_syms[i];
      sym_freq* t = pCur_syms; pCur_syms = pNew_syms; pNew_syms = t;
    }
    return pCur_syms;
  }

  // calculate_minimum_redundancy() originally written by: Alistair Moffat, alistair@cs.mu.oz.au, Jyrki Katajainen, jyrki@diku.dk, November 1996.
  static void calculate_minimum_redundancy(sym_freq *A, int n)
  {
    int root, leaf, next, avbl, used, dpth;
    if (n==0) return; else if (n==1) { A[0].m_key = 1; return; }
    A[0].m_key += A[1].m_key; root = 0; leaf = 2;
    for (next=1; next < n-1; next++)
    {
      if (leaf>=n || A[root].m_key<A[leaf].m_key) { A[next].m_key = A[root].m_key; A[root++].m_key = (uint16)next; } else A[next].m_key = A[leaf++].m_key;
      if (leaf>=n || (root<next && A[root].m_key<A[leaf].m_key)) { A[next].m_key = (uint16)(A[next].m_key + A[root].m_key); A[root++].m_key = (uint16)next; } else A[next].m_key = (uint16)(A[next].m_key + A[leaf++].m_key);
    }
    A[n-2].m_key = 0; for (next=n-3; next>=0; next--) A[next].m_key = A[A[next].m_key].m_key+1;
    avbl = 1; used = dpth = 0; root = n-2; next = n-1;
    while (avbl>0)
    {
      while (root>=0 && (int)A[root].m_key==dpth) { used++; root--; }
      while (avbl>used) { A[next--].m_key = (uint16)(dpth); avbl--; }
      avbl = 2*used; dpth++; used = 0;
    }
  }

  // Limits canonical Huffman code table's max code size.
  enum { MAX_SUPPORTED_HUFF_CODESIZE = 32 };
  static void huffman_enforce_max_code_size(int *pNum_codes, int code_list_len, int max_code_size)
  {
    if (code_list_len <= 1) return;
    for (int i = max_code_size + 1; i <= MAX_SUPPORTED_HUFF_CODESIZE; i++) pNum_codes[max_code_size] += pNum_codes[i];
    uint32 total = 0; for (int i = max_code_size; i > 0; i--) total += (((uint32)pNum_codes[i]) << (max_code_size - i));
    while (total != (1UL << max_code_size))
    {
      pNum_codes[max_code_size]--; 
      for (int i = max_code_size - 1; i > 0; i--) if (pNum_codes[i]) { pNum_codes[i]--; pNum_codes[i + 1] += 2; break; } 
      total--;
    }
  }

  void compressor::optimize_huffman_table(int table_num, int table_len, int code_size_limit)
  {
    sym_freq syms0[MAX_HUFF_SYMBOLS], syms1[MAX_HUFF_SYMBOLS];
    
    int num_used_syms = 0;
    const uint16 *pSym_count = &m_huff_count[table_num][0];
    for (int i = 0; i < table_len; i++) if (pSym_count[i]) { syms0[num_used_syms].m_key = (uint16)pSym_count[i]; syms0[num_used_syms++].m_sym_index = (uint16)i; }
      
    sym_freq* pSyms = radix_sort_syms(num_used_syms, syms0, syms1); calculate_minimum_redundancy(pSyms, num_used_syms);

    int num_codes[1 + MAX_SUPPORTED_HUFF_CODESIZE]; clear_obj(num_codes); for (int i = 0; i < num_used_syms; i++) num_codes[pSyms[i].m_key]++;

    huffman_enforce_max_code_size(num_codes, num_used_syms, code_size_limit);

    clear_obj(m_huff_code_sizes[table_num]); clear_obj(m_huff_codes[table_num]); 
    for (int i = 1, j = num_used_syms; i <= code_size_limit; i++) 
      for (int l = num_codes[i]; l > 0; l--) m_huff_code_sizes[table_num][pSyms[--j].m_sym_index] = static_cast<uint8>(i);

    uint next_code[MAX_SUPPORTED_HUFF_CODESIZE + 1]; next_code[1] = 0;
    for (int j = 0, i = 2; i <= code_size_limit; i++) next_code[i] = j = ((j + num_codes[i - 1]) << 1);

    for (int i = 0; i < table_len; i++)
    {
      uint code_size; if ((code_size = m_huff_code_sizes[table_num][i]) == 0) continue;
      uint rev_code = 0, code = next_code[code_size]++;
      for (int l = code_size; l > 0; l--, code >>= 1) rev_code = (rev_code << 1) | (code & 1);
      m_huff_codes[table_num][i] = static_cast<uint16>(rev_code);
    }
  }
  
  inline void compressor::flush_output_buffer()
  {
    if ((m_all_writes_succeeded) && (m_pOutput_buf > m_output_buf))
      m_all_writes_succeeded = m_pStream->put_buf(m_output_buf, static_cast<int>(m_pOutput_buf - m_output_buf));
    m_pOutput_buf = m_output_buf;
  }

#define TDEFL_PUT_BITS(b, l) do { uint bits = b; uint len = l; TDEFL_ASSERT(bits <= ((1U << len) - 1U)); m_bit_buffer |= (bits << m_bits_in); m_bits_in += len; \
  while (m_bits_in >= 8) { *m_pOutput_buf++ = static_cast<uint8>(m_bit_buffer); if (m_pOutput_buf == &m_output_buf[OUT_BUF_SIZE]) flush_output_buffer(); m_bit_buffer >>= 8; m_bits_in -= 8; } } while (0)
  
#define TDEFL_RLE_PREV_CODE_SIZE() { if (rle_repeat_count) { \
    if (rle_repeat_count < 3) { \
      m_huff_count[2][prev_code_size] = (uint16)(m_huff_count[2][prev_code_size] + rle_repeat_count); \
      while (rle_repeat_count--) packed_code_sizes[num_packed_code_sizes++] = prev_code_size; \
    } else { \
      m_huff_count[2][16] = (uint16)(m_huff_count[2][16] + 1); packed_code_sizes[num_packed_code_sizes++] = 16; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_repeat_count - 3); \
  } rle_repeat_count = 0; } }

#define TDEFL_RLE_ZERO_CODE_SIZE() { if (rle_z_count) { \
    if (rle_z_count < 3) { \
      m_huff_count[2][0] = (uint16)(m_huff_count[2][0] + rle_z_count); while (rle_z_count--) packed_code_sizes[num_packed_code_sizes++] = 0; \
    } else if (rle_z_count <= 10) { \
      m_huff_count[2][17] = (uint16)(m_huff_count[2][17] + 1); packed_code_sizes[num_packed_code_sizes++] = 17; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_z_count - 3); \
    } else { \
      m_huff_count[2][18] = (uint16)(m_huff_count[2][18] + 1); packed_code_sizes[num_packed_code_sizes++] = 18; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_z_count - 11); \
  } rle_z_count = 0; } }
  
  void compressor::start_dynamic_block(bool last_block)
  {
    optimize_huffman_table(0, MAX_HUFF_SYMBOLS_0, 15); optimize_huffman_table(1, MAX_HUFF_SYMBOLS_1, 15);
    
    int num_lit_codes; for (num_lit_codes = 286; num_lit_codes > 257; num_lit_codes--) if (m_huff_code_sizes[0][num_lit_codes - 1]) break;
    int num_dist_codes; for (num_dist_codes = 30; num_dist_codes > 1; num_dist_codes--) if (m_huff_code_sizes[1][num_dist_codes - 1]) break;

    uint8 code_sizes_to_pack[MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1], packed_code_sizes[MAX_HUFF_SYMBOLS_0 + MAX_HUFF_SYMBOLS_1], prev_code_size = 0xFF;
    memcpy(code_sizes_to_pack, &m_huff_code_sizes[0][0], num_lit_codes);
    memcpy(code_sizes_to_pack + num_lit_codes, &m_huff_code_sizes[1][0], num_dist_codes);
    uint total_code_sizes_to_pack = num_lit_codes + num_dist_codes, num_packed_code_sizes = 0, rle_z_count = 0, rle_repeat_count = 0;

    memset(&m_huff_count[2][0], 0, sizeof(m_huff_count[2][0]) * MAX_HUFF_SYMBOLS_2);
    for (uint i = 0; i < total_code_sizes_to_pack; i++)
    {
      uint8 code_size = code_sizes_to_pack[i];
      if (!code_size)
      {
        TDEFL_RLE_PREV_CODE_SIZE();
        if (++rle_z_count == 138) { TDEFL_RLE_ZERO_CODE_SIZE(); }
      }
      else
      {
        TDEFL_RLE_ZERO_CODE_SIZE();
        if (code_size != prev_code_size)
        {
          TDEFL_RLE_PREV_CODE_SIZE();
          m_huff_count[2][code_size] = (uint16)(m_huff_count[2][code_size] + 1); packed_code_sizes[num_packed_code_sizes++] = code_size;
        }
        else if (++rle_repeat_count == 6)
        {
          TDEFL_RLE_PREV_CODE_SIZE();
        }
      }
      prev_code_size = code_size;
    }
    if (rle_repeat_count) { TDEFL_RLE_PREV_CODE_SIZE(); } else { TDEFL_RLE_ZERO_CODE_SIZE(); }

    optimize_huffman_table(2, MAX_HUFF_SYMBOLS_2, 7);
            
    TDEFL_PUT_BITS(last_block, 1); TDEFL_PUT_BITS(2, 2); TDEFL_PUT_BITS(num_lit_codes - 257, 5); TDEFL_PUT_BITS(num_dist_codes - 1, 5);

    static const uint8 s_packed_code_size_syms_swizzle[] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    int num_bit_lengths; for (num_bit_lengths = 18; num_bit_lengths >= 0; num_bit_lengths--) if (m_huff_code_sizes[2][s_packed_code_size_syms_swizzle[num_bit_lengths]]) break;
    num_bit_lengths = TDEFL_MAX(4, (num_bit_lengths + 1)); TDEFL_PUT_BITS(num_bit_lengths - 4, 4);
    for (int i = 0; i < num_bit_lengths; i++) TDEFL_PUT_BITS(m_huff_code_sizes[2][s_packed_code_size_syms_swizzle[i]], 3);

    for (uint packed_code_sizes_index = 0; packed_code_sizes_index < num_packed_code_sizes; )
    {
      uint code = packed_code_sizes[packed_code_sizes_index++]; TDEFL_ASSERT(code < MAX_HUFF_SYMBOLS_2);
      TDEFL_PUT_BITS(m_huff_codes[2][code], m_huff_code_sizes[2][code]);
      if (code >= 16) TDEFL_PUT_BITS(packed_code_sizes[packed_code_sizes_index++], "\02\03\07"[code - 16]);
    }
  }

  void compressor::flush_block(bool last_block)
  {
    *m_pLZ_flags = static_cast<uint8>(*m_pLZ_flags >> m_num_flags_left); m_pLZ_code_buf -= (m_num_flags_left == 8);

    memset(&m_huff_count[0][0], 0, sizeof(m_huff_count[0][0]) * MAX_HUFF_SYMBOLS_0); memset(&m_huff_count[1][0], 0, sizeof(m_huff_count[1][0]) * MAX_HUFF_SYMBOLS_1);
    for (uint pass = 0; pass < 2; pass++)
    {
      uint flags = 1; 
      for (uint8 *pLZ_codes = m_lz_code_buf; pLZ_codes < m_pLZ_code_buf; flags >>= 1)
      {
        if (flags == 1) flags = *pLZ_codes++ | 0x100;
        if (flags & 1)
        {
          uint match_len = pLZ_codes[0], match_dist = (pLZ_codes[1] | (pLZ_codes[2] << 8)); pLZ_codes += 3;
          if (!pass)
          {
            m_huff_count[0][s_len_sym[match_len]]++;
            if (match_dist < 512)
              m_huff_count[1][s_small_dist_sym[match_dist]]++;
            else
              m_huff_count[1][s_large_dist_sym[match_dist >> 8]]++;
          }
          else
          {
            TDEFL_PUT_BITS(m_huff_codes[0][s_len_sym[match_len]], m_huff_code_sizes[0][s_len_sym[match_len]]); TDEFL_PUT_BITS(match_len & ((1 << s_len_extra[match_len]) - 1), s_len_extra[match_len]);
            uint sym, num_extra_bits;
            if (match_dist < 512)
            {
              sym = s_small_dist_sym[match_dist]; num_extra_bits = s_small_dist_extra[match_dist];
            }
            else
            {
              sym = s_large_dist_sym[match_dist >> 8]; num_extra_bits = s_large_dist_extra[match_dist >> 8];
            }
            TDEFL_PUT_BITS(m_huff_codes[1][sym], m_huff_code_sizes[1][sym]); TDEFL_PUT_BITS(match_dist & ((1 << num_extra_bits) - 1), num_extra_bits);
          }
        }
        else
        {
          uint lit = *pLZ_codes++;
          if (!pass)
            m_huff_count[0][lit]++;
          else
            TDEFL_PUT_BITS(m_huff_codes[0][lit], m_huff_code_sizes[0][lit]);
        }
      }
            
      if (!pass)
      {
        m_huff_count[0][256]++; start_dynamic_block(last_block);
      }
      else
        TDEFL_PUT_BITS(m_huff_codes[0][256], m_huff_code_sizes[0][256]);
    }
    
    if ((last_block) && (m_bits_in & 7)) { TDEFL_PUT_BITS(0, 8 - m_bits_in); }

    m_pLZ_code_buf = m_lz_code_buf + 1; m_pLZ_flags = m_lz_code_buf; m_num_flags_left = 8;
  }

  inline void compressor::record_literal(uint8 lit)
  {
    *m_pLZ_code_buf++ = lit;
    *m_pLZ_flags = static_cast<uint8>(*m_pLZ_flags >> 1); if (--m_num_flags_left == 0) { m_num_flags_left = 8; m_pLZ_flags = m_pLZ_code_buf++; }
    if (m_pLZ_code_buf > &m_lz_code_buf[LZ_CODE_BUF_SIZE - 4]) flush_block(false);
  }

  inline void compressor::record_match(uint match_len, uint match_dist)
  {
    TDEFL_ASSERT((match_len >= MIN_MATCH_LEN) && (match_dist >= 1) && (match_dist <= LZ_DICT_SIZE));
    m_pLZ_code_buf[0] = static_cast<uint8>(match_len - MIN_MATCH_LEN); match_dist -= 1; m_pLZ_code_buf[1] = static_cast<uint8>(match_dist & 0xFF); m_pLZ_code_buf[2] = static_cast<uint8>(match_dist >> 8);
    m_pLZ_code_buf += 3;
    *m_pLZ_flags = static_cast<uint8>((*m_pLZ_flags >> 1) | 0x80); if (--m_num_flags_left == 0) { m_num_flags_left = 8; m_pLZ_flags = m_pLZ_code_buf++; }
    if (m_pLZ_code_buf > &m_lz_code_buf[LZ_CODE_BUF_SIZE - 4]) flush_block(false);
  }

  inline void compressor::find_match(uint pos, uint max_dist, uint max_match_len, uint &match_dist, uint &match_len)
  {
    TDEFL_ASSERT(max_match_len <= MAX_MATCH_LEN); if (max_match_len <= match_len) return;
    uint probe_len, probe_pos = pos, prev_dist = 0, num_probes_left = m_max_probes, next_probe_pos, dist;
    const uint8 *r = m_dict + pos;
    uint8 c0 = m_dict[pos + match_len], c1 = m_dict[pos + match_len - 1];
    for ( ; ; )
    {
      for ( ; ; )
      {
        if (num_probes_left-- == 0) return;
        #define TDEFL_PROBE \
          next_probe_pos = m_next[probe_pos]; if (static_cast<int16>(next_probe_pos) < 0) return; \
          dist = (pos - next_probe_pos) & LZ_DICT_SIZE_MASK; \
          if ((dist > max_dist) || (dist <= prev_dist)) { m_next[probe_pos] = 0xFFFF; return; } \
          prev_dist = dist; probe_pos = next_probe_pos; \
          if ((m_dict[probe_pos + match_len] == c0) && (m_dict[probe_pos + match_len - 1] == c1)) break;
        TDEFL_PROBE; TDEFL_PROBE; TDEFL_PROBE;
      }
      const uint8 *p = r, *q = m_dict + probe_pos; for (probe_len = 0; probe_len < max_match_len; probe_len++) if (*p++ != *q++) break;
      if (probe_len > match_len)
      {
        match_dist = prev_dist; if ((match_len = probe_len) == max_match_len) return;
        c0 = m_dict[pos + match_len]; c1 = m_dict[pos + match_len - 1];
      }
    }
  }

  bool compressor::compress_data(const void *pData, uint data_len)
  {
    if ((!m_pStream) || (!m_all_writes_succeeded)) return false;
    const uint8 *pSrc = static_cast<const uint8*>(pData); if (m_flags & WRITE_ZLIB_HEADER) { m_adler32 = adler32(pSrc, data_len, m_adler32); }
    while ((data_len) || ((!pSrc) && (m_lookahead_size)))
    {
      // Update dictionary and hash chains. Keeps the lookahead size equal to MAX_MATCH_LEN.
      if (m_lookahead_size >= (MIN_MATCH_LEN - 1))
      {
        // Alternate dictionary update loop (avoids a load hit stores per iteration).
        uint dst_pos = (m_lookahead_pos + m_lookahead_size) & LZ_DICT_SIZE_MASK;
        uint ins_pos = (dst_pos - 2) & LZ_DICT_SIZE_MASK;
        uint hash = (m_dict[ins_pos] << 4) ^ m_dict[(ins_pos + 1) & LZ_DICT_SIZE_MASK];
        uint num_bytes_to_process = TDEFL_MIN(data_len, MAX_MATCH_LEN - m_lookahead_size);
        const uint8 *pSrc_end = pSrc + num_bytes_to_process;
        data_len -= num_bytes_to_process;  m_lookahead_size += num_bytes_to_process;
        while (pSrc != pSrc_end)
        {
          uint8 c = *pSrc++; m_dict[dst_pos] = c; if (dst_pos < (MAX_MATCH_LEN - 1)) m_dict[LZ_DICT_SIZE + dst_pos] = c;
          hash = ((hash << 4) ^ c) & (LZ_HASH_SIZE - 1);
          m_next[ins_pos] = m_hash[hash]; m_hash[hash] = static_cast<uint16>(ins_pos);
          dst_pos = (dst_pos + 1) & LZ_DICT_SIZE_MASK; ins_pos = (ins_pos + 1) & LZ_DICT_SIZE_MASK;
        }
      }
      else
      {
        while ((data_len) && (m_lookahead_size < MAX_MATCH_LEN))
        {
          uint8 c = *pSrc++; data_len--;
          uint dst_pos = (m_lookahead_pos + m_lookahead_size) & LZ_DICT_SIZE_MASK;
          m_dict[dst_pos] = c; if (dst_pos < (MAX_MATCH_LEN - 1)) m_dict[LZ_DICT_SIZE + dst_pos] = c;
          if (++m_lookahead_size >= MIN_MATCH_LEN)
          {
            uint ins_pos = (dst_pos - 2) & LZ_DICT_SIZE_MASK;
            uint hash = ((m_dict[ins_pos] << 8) ^ (m_dict[(ins_pos + 1) & LZ_DICT_SIZE_MASK] << 4) ^ c) & (LZ_HASH_SIZE - 1);
            m_next[ins_pos] = m_hash[hash]; m_hash[hash] = static_cast<uint16>(ins_pos);
          }
        }
      }          
      m_dict_size = TDEFL_MIN(LZ_DICT_SIZE - m_lookahead_size, m_dict_size);
      if ((pSrc) && (m_lookahead_size < MAX_MATCH_LEN)) break;
      
      // Simple lazy/greedy parsing state machine.
      uint len_to_move = 1, cur_match_dist = 0, cur_match_len = m_saved_match_len ? m_saved_match_len : (MIN_MATCH_LEN - 1);
      find_match(m_lookahead_pos, m_dict_size, m_lookahead_size, cur_match_dist, cur_match_len);
      if ((cur_match_len == MIN_MATCH_LEN) && (cur_match_dist >= 12U*1024U)) { cur_match_dist = cur_match_len = 0; } // reject really far small matches as not worth using
      if (m_saved_match_len)
      {
        if (cur_match_len > m_saved_match_len)
        {
          record_literal((uint8)m_saved_lit);
          if (cur_match_len >= 64)
          {
            record_match(cur_match_len, cur_match_dist);
            m_saved_match_len = 0; len_to_move = cur_match_len;
          }
          else
          {
            m_saved_lit = m_dict[m_lookahead_pos]; m_saved_match_dist = cur_match_dist; m_saved_match_len = cur_match_len; len_to_move = 1;
          }
        }
        else
        {
          record_match(m_saved_match_len, m_saved_match_dist);
          len_to_move = m_saved_match_len - 1; m_saved_match_len = 0;
        }
      }
      else if (!cur_match_dist)
        record_literal(m_dict[m_lookahead_pos]);
      else if ((m_greedy_parsing) || (cur_match_len >= 64))
      {
        record_match(cur_match_len, cur_match_dist);
        len_to_move = cur_match_len;
      }
      else
      {
        m_saved_lit = m_dict[m_lookahead_pos]; m_saved_match_dist = cur_match_dist; m_saved_match_len = cur_match_len;
      }
      // Move the lookahead forward by len_to_move bytes.
      m_lookahead_pos = (m_lookahead_pos + len_to_move) & LZ_DICT_SIZE_MASK;
      TDEFL_ASSERT(m_lookahead_size >= len_to_move); m_lookahead_size -= len_to_move;
      m_dict_size = TDEFL_MIN(m_dict_size + len_to_move, LZ_DICT_SIZE);
    }
    if (!pData)
    {
      if (m_saved_match_len) record_match(m_saved_match_len, m_saved_match_dist);
      flush_block(true);
      if (m_flags & WRITE_ZLIB_HEADER) { for (uint i = 0; i < 4; i++) { TDEFL_PUT_BITS((m_adler32 >> 24) & 0xFF, 8); m_adler32 <<= 8; } }
      flush_output_buffer(); m_pStream = NULL;
    }
    return m_all_writes_succeeded;
  }

  bool compressor::init(output_stream *pStream, int flags)
  {
    if (!pStream) return false;
    m_pStream = pStream; m_flags = static_cast<uint>(flags); m_max_probes = ((flags & 0xFFF) + 2) / 3; m_greedy_parsing = (flags & GREEDY_PARSING_FLAG) != 0;
    if (!(flags & NONDETERMINISTIC_PARSING_FLAG)) memset(m_hash, 0xFF, sizeof(m_hash));
    m_lookahead_pos = 0; m_lookahead_size = 0; m_dict_size = 0;
    m_pLZ_code_buf = m_lz_code_buf + 1; m_pLZ_flags = m_lz_code_buf; m_num_flags_left = 8;
    m_pOutput_buf = m_output_buf; m_bits_in = 0; m_bit_buffer = 0; m_all_writes_succeeded = true;
    m_saved_match_dist = 0, m_saved_match_len = 0, m_saved_lit = 0; m_adler32 = 1;
    if (m_flags & WRITE_ZLIB_HEADER) { TDEFL_PUT_BITS(0x78, 8); TDEFL_PUT_BITS(1, 8); }
    return m_all_writes_succeeded;
  }

  // ------------------- High-level helpers (only the below methods use the heap in any way).
  void expandable_malloc_output_stream::init(size_t initial_capacity)
  {
    clear();
    if (initial_capacity)
    {
      m_pBuf = TDEFL_MALLOC(initial_capacity);
      m_capacity = m_pBuf ? initial_capacity : 0; 
    }
  }
    
  void expandable_malloc_output_stream::clear() 
  { 
    TDEFL_FREE(m_pBuf); m_size = m_capacity = 0; 
  }

  bool expandable_malloc_output_stream::put_buf(const void* pBuf, int len)
  {
    size_t new_size = m_size + len;
    if (new_size > m_capacity)
    {
      size_t new_capacity = m_capacity; do { new_capacity = TDEFL_MAX(128U, new_capacity << 1U); } while (new_size > new_capacity);
      void *pNew_buf = TDEFL_REALLOC(m_pBuf, new_capacity); if (!pNew_buf) return false;
      m_pBuf = pNew_buf; m_capacity = new_capacity;
    }
    memcpy((uint8*)m_pBuf + m_size, pBuf, len); m_size = new_size;
    return true;
  }

  bool buffer_output_stream::put_buf(const void* pBuf, int len)
  {
    size_t new_size = m_size + len; if (new_size > m_capacity) return false;
    memcpy((uint8*)m_pBuf + m_size, pBuf, len); m_size = new_size;
    return true;
  }

  bool compress_mem_to_output_stream(const void *pBuf, size_t buf_len, output_stream *pStream, int flags)
  {
    if ((buf_len) && (!pBuf)) return false;
    compressor *pComp = TDEFL_NEW compressor;
    bool succeeded = pComp->init(pStream, flags);
    while (buf_len)
    {
      uint n = static_cast<uint>(TDEFL_MIN(16U * 1024U * 1024U, buf_len)); succeeded = succeeded && pComp->compress_data(pBuf, n); if (!succeeded) break;
      pBuf = static_cast<const uint8*>(pBuf) + n; buf_len -= n;
    }
    succeeded = succeeded && pComp->compress_data(NULL, 0);
    TDEFL_DELETE pComp; return succeeded;
  }
   
  void *compress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len, size_t *pOut_len, int flags)
  {
    if (!pOut_len) return false; else *pOut_len = 0;
    expandable_malloc_output_stream out_stream(TDEFL_MAX(32U, src_buf_len >> 1U));
    if (!compress_mem_to_output_stream(pSrc_buf, src_buf_len, &out_stream, flags)) return NULL;
    *pOut_len = out_stream.get_size();
    void *p = out_stream.assume_buf_ownership();
    return p;
  }

  size_t compress_mem_to_mem(void *pOut_buf, size_t out_buf_len, const void *pSrc_buf, size_t src_buf_len, int flags)
  {
    buffer_output_stream out_stream(pOut_buf, out_buf_len);
    if (!compress_mem_to_output_stream(pSrc_buf, src_buf_len, &out_stream, flags)) return 0;
    return out_stream.get_size();
  }
  
  void *write_image_to_png_file_in_memory(const void *pImage, int w, int h, int num_chans, uint32 *pLen_out) 
  {
    *pLen_out = 0; const int bpl = w * num_chans; compressor *pComp = TDEFL_NEW compressor; expandable_malloc_output_stream out_stream(57+TDEFL_MAX(64U, (1+bpl)*h)); 
    // write dummy header
    int z; for (z = 41; z; --z) out_stream.put_buf(&z, 1);
    // compress image data
    pComp->init(&out_stream, tinydeflate::DEFAULT_MAX_PROBES | tinydeflate::WRITE_ZLIB_HEADER);
    for (int y = 0; y < h; ++y) { pComp->compress_data(&z, 1); pComp->compress_data((uint8*)pImage + y * bpl, bpl); }
    pComp->compress_data(NULL, 0); if (!pComp->get_all_writes_succeeded()) { TDEFL_DELETE pComp; return NULL; }
    // write real header
    *pLen_out = (uint32)(out_stream.get_size()-41);
    uint8 pnghdr[41]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,
      0,0,(uint8)(w>>8),(uint8)w,0,0,(uint8)(h>>8),(uint8)h,8,"\0\0\04\02\06"[num_chans],0,0,0,0,0,0,0,
      (uint8)(*pLen_out>>24),(uint8)(*pLen_out>>16),(uint8)(*pLen_out>>8),(uint8)*pLen_out,0x49,0x44,0x41,0x54};
    uint32 c=crc32(pnghdr+12,17); for (int i=0; i<4; ++i, c<<=8) ((uint8*)(pnghdr+29))[i]=(uint8)(c>>24);
    memcpy(out_stream.get_buf(), pnghdr, 41); 
    // write footer (IDAT CRC-32, followed by IEND chunk)
    if (!out_stream.put_buf("\0\0\0\0\0\0\0\0\x49\x45\x4e\x44\xae\x42\x60\x82", 16)) { *pLen_out = 0; TDEFL_DELETE pComp; return NULL; }
    c = crc32(out_stream.get_buf()+41-4, *pLen_out+4); for (int i=0; i<4; ++i, c<<=8) (out_stream.get_buf()+out_stream.get_size()-16)[i] = (uint8)(c >> 24);
    // compute final size of file, grab compressed data buffer and return
    *pLen_out += 57; void *pBuf = out_stream.assume_buf_ownership(); TDEFL_DELETE pComp; return pBuf;
  }

} // namespace tinydeflate

#endif // TINYDEFLATE_HEADER_FILE_ONLY
