// Copies objects out of the OCaml heap where they are not managed by the GC

#include <stdio.h>
#include <memory.h>
#include <assert.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include <caml/address_class.h>
#include <caml/alloc.h>
#include <caml/config.h>
#include <caml/custom.h>
#include <caml/fail.h>
#include <caml/gc.h>
#include <caml/memory.h>
#include <caml/mlvalues.h>
#ifdef __cplusplus
} /* extern "C" */
#endif

#include "offheap.h"

#define ENTRIES_PER_QUEUE_CHUNK 4096

#define Ptr_val(v) ((void *)((v) & ~1))
#define Val_ptr(v) ((value)(((uintptr_t)(v)) | 1))
#define DEBUG

#ifdef DEBUG
#include <stdarg.h>
static char padding[1024] = {'\0'};
static size_t depth = 0;
#define LOG(...)                  \
  fprintf(stderr, "%s", padding); \
  fprintf(stderr, __VA_ARGS__)
#define ENTER()                                                       \
  LOG("> %s (in %s at line %d)\n", __FUNCTION__, __FILE__, __LINE__); \
  padding[depth++] = ' '
#define LEAVE()            \
  padding[--depth] = '\0'; \
  LOG("< %s (in %s at line %d)\n", __FUNCTION__, __FILE__, __LINE__)
#else
#define ENTER()
#define LOG(...)
#define LEAVE()
#endif

/**
 * Item stored in the queue.
 */
typedef struct queue_item
{
  /// Pointer to the copied block.
  value block;
  /// Saved header value.
  header_t header;
} queue_item_t;

/**
 * Linked list of blocks of pointers.
 */
typedef struct queue_chunk
{
  /// Link to the next chunk storing the queue.
  struct queue_chunk *next;
  /// Storage for a number of pointer.
  struct queue_item entries[ENTRIES_PER_QUEUE_CHUNK];
} queue_chunk_t;

/**
 * Wrapper on top of the queue implementing iteration.
 */
typedef struct queue
{
  /// Pointer to the head of the queue.
  queue_chunk_t first_chunk;
  /// Pointer to the chunk being read.
  queue_chunk_t *read_chunk;
  /// Pointer to the chunk written.
  queue_chunk_t *write_chunk;
  /// Index into the read chunk.
  int read_pos;
  /// Index into the written chunk.
  int write_pos;
} queue_t;

/**
 * Default allocator: invokes malloc.
 */
void *offheap_alloc(value allocator, size_t size)
{
  ENTER();
  void *ptr = malloc(size);
  if (ptr != NULL)
  {
    if(caml_page_table_add(In_static_data, ptr, (void *)((uintptr_t)ptr + size)) != 0)
    {
      free(ptr);
      ptr = NULL;
    }
  }
  LEAVE();
  return ptr;
}

/**
 * Default deallocator: invokes free.
 */
void offheap_free(value allocator, void *ptr, size_t size)
{
  ENTER();
  assert(ptr != NULL);
  caml_page_table_remove(In_static_data, ptr, (void *)((uintptr_t)ptr + size));
  free(ptr);
  LEAVE();
}

/**
 * Creates a new queue.
 */
void queue_init(queue_t *q)
{
  ENTER();
  q->first_chunk.next = NULL;
  q->read_chunk = &q->first_chunk;
  q->write_chunk = &q->first_chunk;
  q->read_pos = 0;
  q->write_pos = 0;
  LEAVE();
}

/**
 * Adds an item to the end of the queue.
 */
int queue_push(queue_t *q, value v, header_t hdr)
{
  ENTER();
  if (q->write_pos == ENTRIES_PER_QUEUE_CHUNK)
  {
    struct queue_chunk *new_chunk = (struct queue_chunk *)malloc(sizeof(struct queue_chunk));
    if (new_chunk == NULL)
    {
      caml_raise_out_of_memory();
      LEAVE();
      return -1;
    }
    new_chunk->next = NULL;
    q->write_chunk->next = new_chunk;
    q->write_pos = 0;
    q->write_chunk = new_chunk;
  }

  struct queue_item *item = &q->write_chunk->entries[q->write_pos++];
  item->block = v;
  item->header = hdr;
  LEAVE();
  return 0;
}

/**
 * Returns the next item from the front of the queue.
 */
queue_item_t queue_pop(queue_t *q)
{
  ENTER();
  if (q->read_pos == ENTRIES_PER_QUEUE_CHUNK)
  {
    q->read_pos = 0;
    q->read_chunk = q->read_chunk->next;
  }
  LEAVE();
  return q->read_chunk->entries[q->read_pos++];
}

/**
 * Checks if there are any more items to read.
 */
int queue_empty(queue_t *q)
{
  ENTER();
  int empty = q->read_pos == q->write_pos && q->read_chunk == q->write_chunk;
  LEAVE();
  return empty;
}

/**
 * Resets the front pointer to the start of the queue.
 */
void queue_reset(queue_t *q)
{
  ENTER();
  q->read_pos = 0;
  q->read_chunk = &q->first_chunk;
  LEAVE();
}

/**
 * Frees storage used by the queue.
 */
void queue_free(queue_t *q)
{
  ENTER();
  struct queue_chunk *chunk = q->first_chunk.next;
  while (chunk)
  {
    struct queue_chunk *next = chunk->next;
    free(chunk);
    chunk = next;
  }
  LEAVE();
}

/**
 * Checks if the object can be copied.
 */
extern struct custom_operations caml_int32_ops;
extern struct custom_operations caml_int64_ops;
extern struct custom_operations caml_nativeint_ops;
static inline int isObjectValid(value val)
{
  ENTER();
  int valid = 0;
  struct custom_operations *ops = NULL;
  if (Is_block(val))
  {
    switch (Tag_val(val))
    {
    case Abstract_tag:
      // Abstract objects definitely cannot be copied.
      valid = 0;
      break;
    case Custom_tag:
      // Some custom objects present in the runtime can be simply copied.
      ops = Custom_ops_val(val);

      if ((ops == &caml_int32_ops) ||
          (ops == &caml_int64_ops) ||
          (ops == &caml_nativeint_ops))
      {
        valid = 1;
      }
      break;
    default:
      // All other objects can be copied.
      valid = 1;
      break;
    }
  }
  LEAVE();
  return valid;
}

/**
 * Checks if a value can be copied.
 */
static inline int shouldCopy(value v, int copyStatic)
{
  ENTER();
  int copy = 0;
  if (Is_block(v))
  {
    int kind = Classify_addr(v);
    if ((kind & In_heap) | (kind & In_young))
    {
      copy = 1;
    }
    else if (kind & In_static_data)
    {
      copy = copyStatic;
    }
    else
    {
      copy = 0;
    }
  }
  LEAVE();
  return copy;
}

/**
 * Traverses the object graph, and marks the interesting objects.
 * Returns the total size of the object graph
 */
intnat offheap_mark(
    value v,
    struct queue *q,
    int copyStatic)
{
  ENTER();
  queue_init(q);

  intnat size = 0;

  // Adjust the pointer for infix values.
  if (Is_block(v) && Tag_val(v) == Infix_tag)
  {
    v -= Infix_offset_hd(Hd_val(v));
  }

  // Ensure the value can be copied.
  if (isObjectValid(v))
  {
    // Push the first item with offset 0 and marks its header.
    if (queue_push(q, v, Hd_val(v)) != 0)
    {
      size = -1;
    }
    else
    {
      Hd_val(v) = Make_header(0, Tag_val(v), Caml_blue);

      // First pass: traverse the object and compute the size of the final copy,
      // in bytes. The queue will contain the unrolled list of all blocks.
      while (!queue_empty(q))
      {
        // Pop the next element from the queue and read information from the header
        // before the size in the header is overwritten with the new offset of the
        // object into the buffer in which the copy will be allocated.
        const queue_item_t item = queue_pop(q);
        const value node = item.block;
        const header_t hd = item.header;
        const mlsize_t sz = Wosize_hd(hd);
        const tag_t tag = Tag_hd(hd);
        LOG("Traversing %lu (size: %lu, tag: %u, offset: %lu)\n", node, sz, tag, size);

        // Advance the pointer by the size of the block. Since we inspect the colour
        // encoded in the header when checking pointers to this object, we keep the
        // header correctly formatted, with colour and tag intact.
        const mlsize_t bytes = Bhsize_hd(hd);
        LOG("Bytes: %lu\n", bytes);
        Hd_val(node) = Make_header(size, tag, Caml_blue);
        size += bytes;

        if (tag < No_scan_tag)
        {
          // Push the interesting fields on the queue.
          for (mlsize_t i = 0; i < sz; ++i)
          {
            const value field = Field(node, i);

            // Skip over primitive fields and non-heap pointers.
            if (!shouldCopy(field, copyStatic))
            {
              continue;
            }

            const header_t fh = Hd_val(field);

            // Skip already visited objects: their header is blue.
            if (Color_hd(fh) == Caml_blue)
            {
              continue;
            }

            // Add the item to the queue and mark the chunk as visited.
            if (queue_push(q, field, fh) != 0)
            {
              size = -1;
              break;
            }
            Hd_val(field) = Bluehd_hd(fh);
          }
        }
      }
    }
  }
  LOG("Total marked size (bytes): %ld (words: %ld)\n", size, size / sizeof(value));
  LEAVE();
  return size;
}

offheap_buffer_t offheap_clone(
    intnat size,
    struct queue *q,
    void *data,
    alloc_t allocFn,
    int copyStatic)
{
  ENTER();

  // Now that the size is known, allocate a contiguous off-heap
  // buffer large enough to hold the entire object.
  const uintptr_t buffer = (uintptr_t)allocFn(data, size);

  // Second pass: Adjust all pointers.
  if (buffer)
  {
    uintptr_t ptr = buffer;
    queue_reset(q);
    while (!queue_empty(q))
    {
      // Pop the pointer and its old header.
      const queue_item_t item = queue_pop(q);
      const value node = item.block;
      const header_t hd = item.header;
      const mlsize_t sz = Wosize_hd(hd);
      const tag_t tag = Tag_hd(hd);

      // Get a pointer to the current value and advance the offset.
      const value dst = Val_hp((void *)ptr);
      Hd_val(dst) = hd;
      ptr += Bhsize_hd(hd);

      // Set up the new object.
      if (tag < No_scan_tag)
      {
        // Regular object - copy field by field.
        for (mlsize_t i = 0; i < sz; ++i)
        {
          value field = Field(node, i);

          if (!shouldCopy(field, copyStatic))
          {
            // Simply copy over primitives and off-heap pointers.
            Field(dst, i) = field;
          }
          else
          {
            if (Tag_hd(Hd_val(field)) == Infix_tag)
            {
              field -= Infix_offset_hd(Hd_val(field));
            }

            // At this point, all pointed-to objects should be coloured.
            const header_t fd = Hd_val(field);
            CAMLassert(Is_blue_hd(fd));

            // Size field of header stores the offset into the new buffer.
            const uintptr_t addr = buffer + Wosize_hd(fd);
            Field(dst, i) = Val_hp((void *)addr);
          }
        }
      }
      else
      {
        // If not a tuple of fields, copy raw data.
        memcpy(Bp_val(dst), Bp_val(node), Bosize_hd(hd));
      }
    }

    CAMLassert(ptr == buffer + size);
  }
  else
  {
    caml_raise_out_of_memory();
  }
  offheap_buffer_t result;
  result.ptr = size < 0 ? NULL : (void *)buffer;
  result.size = size;
  LEAVE();
  return result;
}

void offheap_unmark(struct queue *q)
{
  ENTER();
  // Reset the values in the Ocaml heap.
  queue_reset(q);
  while (!queue_empty(q))
  {
    queue_item_t item = queue_pop(q);
    Hd_val(item.block) = item.header;
  }
  queue_free(q);
  LEAVE();
}
#ifdef __cplusplus
extern "C"
{
#endif
  size_t offheap_words(value v, int copyStatic)
  {
    ENTER();
    static struct queue q;
    size_t result = offheap_mark(v, &q, copyStatic);
    offheap_unmark(&q);
    LEAVE();
    return result;
  }

  offheap_buffer_t offheap_copy(
      value v,
      void *data,
      alloc_t allocFn,
      int copyStatic)
  {
    ENTER();
    static struct queue q;
    size_t size = offheap_mark(v, &q, copyStatic);
    offheap_buffer_t result = offheap_clone(size, &q, data, allocFn, copyStatic);
    offheap_unmark(&q);
    LEAVE();
    return result;
  }


  CAMLprim intnat caml_offheap_words_untagged(value copyStatic, value obj)
  {
    ENTER();
    assert(copyStatic == Val_true || copyStatic == Val_false);
    intnat bytes = offheap_words(obj, copyStatic == Val_true);
    LEAVE();
    return bytes / sizeof(value);
  }

  CAMLprim value caml_offheap_words(value copyStatic, value obj)
  {
    ENTER();
    assert(copyStatic == Val_true || copyStatic == Val_false);
    intnat words = caml_offheap_words_untagged(copyStatic, obj);
    LEAVE();
    return Val_long(words);
  }

  CAMLprim value caml_offheap_copy_with_alloc(value copyStatic, value allocator, value obj)
  {
    ENTER();
    assert(copyStatic == Val_true || copyStatic == Val_false);
    CAMLparam3(copyStatic, allocator, obj);
    CAMLlocal1(proxy);

    // If the object is not on the OCaml heap, return it unchanged.
    if (!Is_block(obj) || !Is_in_heap_or_young(obj))
    {
      CAMLreturn(obj);
    }

    // Fetch the allocator function.
    alloc_t allocFn = (alloc_t)Field(allocator, 0);
    offheap_buffer_t buffer = offheap_copy(obj, (void *)allocator, allocFn, copyStatic == Val_true);

    if (buffer.ptr == NULL)
    {
      caml_invalid_argument("object could not be copied off-heap");
    }

    // Buffer should be allocated to at least a word boundary.
    CAMLassert(((uintptr_t)buffer.ptr & 1) == 0);

    // Create a proxy pointing to the object, storing the allocator as well.
    // The object is traversed by the GC to keep the allocator alive on the heap.
    // The last bit of the pointer is set to 1 to hide it from GC.
    proxy = caml_alloc_small(3, 0);
    Field(proxy, 0) = Val_ptr(buffer.ptr);
    Field(proxy, 1) = Val_int(buffer.size);
    Field(proxy, 2) = allocator;

    LEAVE();
    CAMLreturn(proxy);
  }

  CAMLprim value caml_offheap_get(value obj)
  {
    ENTER();
    CAMLparam1(obj);

    // If the object is not on the OCaml heap, return it unchanged.
    if (!Is_block(obj) || !Is_in_heap_or_young(obj))
    {
      return obj;
    }

    // Fetch the pointer, which should not have been deleted.
    void *ptr = Ptr_val(Field(obj, 0));
    if (ptr == NULL)
    {
      caml_invalid_argument("deleted");
    }

    LEAVE();
    // Return a pointer to the header.
    CAMLreturn(Val_hp(ptr));
  }

  CAMLprim value caml_offheap_delete(value obj)
  {
    ENTER();
    CAMLparam1(obj);
    CAMLlocal1(allocator);

    // If object is not on the OCaml heap, do nothing.
    if (!Is_block(obj) || !Is_in_heap_or_young(obj))
    {
      CAMLreturn(Val_unit);
    }

    // Read the fields of the object.
    void *ptr = Ptr_val(Field(obj, 0));
    size_t size = Int_val(Field(obj, 1));
    allocator = Field(obj, 2);

    // Fetch the pointer, which should not have been deleted.
    if (ptr == NULL)
    {
      caml_invalid_argument("deleted");
    }

    // Free the pointer, which should be a valid malloc'd pointer.
    free_t freeFn = (free_t)Field(allocator, 1);
    freeFn((void *)allocator, (void *)ptr, size);

    // Clear the field and size.
    Field(obj, 0) = Val_long(0);
    Field(obj, 1) = Val_long(0);

    LEAVE();
    CAMLreturn(Val_unit);
  }

  CAMLprim value caml_offheap_get_alloc(value unit)
  {
    ENTER();
    CAMLparam1(unit);
    CAMLlocal1(block);

    // Allocate a block with two fields to hold the default allocator.
    // Custom allocators can store additional data by using a larger object.
    block = caml_alloc_small(2, Abstract_tag);
    Field(block, 0) = (value)offheap_alloc;
    Field(block, 1) = (value)offheap_free;

    LEAVE();
    CAMLreturn(block);
  }
#ifdef __cplusplus
} /* extern "C" */
#endif