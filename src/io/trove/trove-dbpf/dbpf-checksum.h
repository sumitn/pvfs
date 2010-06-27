#include <stdio.h>
#include "trove-types.h"
#include "dbpf-open-cache.h"
#include "quicklist.h"
#include "rbtree.h"

/* Disk block size */
#define BLOCK_SIZE 4096

/* Checksum chunk (block) size */
#define CKSUM_BUFLEN 65536

#define CKSUM_BLK_COUNT (BLOCK_SIZE/sizeof(PVFS_cksum))
#define CKSUM_BLK_BUFLEN (CKSUM_BLK_COUNT * CKSUM_BUFLEN)

#define CKSUM_MULTIPLES_MASK (~((uintptr_t) CKSUM_BUFLEN -1))
#define CKSUM_BLK_MULTIPLES_MASK (~((uintptr_t) CKSUM_BLK_BUFLEN - 1))

#define ALIGNED_CKSUM_OFFSET(__offset) (__offset & CKSUM_MULTIPLES_MASK)
#define ALIGNED_CKSUM_SIZE(__offset, __size)			\
   (((__offset + __size + CKSUM_BUFLEN - 1)			\
     & CKSUM_MULTIPLES_MASK) - ALIGNED_CKSUM_OFFSET(__offset))
#define ALIGNED_CKSUM_BLK_OFFSET(__offset)	\
   (__offset & CKSUM_BLK_MULTIPLES_MASK)
#define ALIGNED_CKSUM_BLK_SIZE(__offset, __size)			\
   (((__offset + __size + CKSUM_BLK_BUFLEN - 1)				\
     & CKSUM_BLK_MULTIPLES_MASK) - ALIGNED_CKSUM_BLK_OFFSET(__offset))
#define IS_ALIGNED_CKSUM(__ptr)						\
   ((((uintptr_t)__ptr) & CKSUM_MULTIPLES_MASK) == (uintptr_t)__ptr)

typedef int32_t PVFS_cktype;
typedef int64_t PVFS_cksum;
typedef int32_t PVFS_magic;

typedef struct cksum_extent
{
   PVFS_offset start;
   PVFS_offset end;
   PVFS_cksum *cksum;
   struct rb_node node;
   struct qlist_head link;
} cksum_extent;

#define CKSUM_READ   LIO_READ
#define CKSUM_WRITE  LIO_WRITE
#define CKSUM_FLUSH  2
#define CKSUM_VERIFY 3

/*!
 * \struct cksum_op
 *
 * This structure holds information about each checksum operation which is
 * being initiated by the trove level. Trove will recieve a pointer to this
 * structure upon the first call of checksum_submit(). This same pointer
 * will be used to complete the operation.
 */
typedef struct cksum_op
{
   int op;                      /*!< operation type: READ/WRITE */
   struct open_cache_ref *ref;  /*!< Cache reference */
   PVFS_offset foffset;         /*!< File offset for checksum */
   PVFS_size bsize;             /*!< buffer size */
   char *data;                  /*!< Checksum data */
   PVFS_cksum *cksum;           /*!< Checksum */
   int count;                   /*!< Number of checksum elements */
   struct qlist_head *link;     /*!< Linked list */
} cksum_op;

/*!
 * \struct file_cksum_buffer 
 *
 * structure to cache checksum data. this structure contains aligned checksum
 * values, depending on block size (4K).
 */
typedef struct file_cksum_buffer
{
   PVFS_size count;        /** Number of checksum blocks for this file */
   int status;             /** dirty?                                  */
   struct rb_root extents;
} file_cksum_buffer;

/* Functions */
/*!
 * Function to generate crc32 checksum for a given buffer using buflen as
 * each iteration's length
 * 
 * \param buffer Buffer for which checksum has to be generated
 * \param size size of buffer
 * \param buflen Buffer length for each iteration for generating checksum
 * \return checksum from crc32 algorithm
 */
PVFS_cksum crc32_generate(unsigned char *buffer, 
			  PVFS_offset buflen);

int checksum_prepare_read(struct cksum_op *cks_op);
int checksum_prepare_write(struct cksum_op *cks_op);

int checksum_verify_read(struct cksum_op *cks_op, unsigned char *buf);
int checksum_complete_write(struct cksum_op *cks_op);

void * checksum_submit_request(struct open_cache_ref *out_ref, 
			       PVFS_offset file_offset, 
			       PVFS_size buffer_size,
			       char *data_buf, int op);
int checksum_complete_request(void *ptr, unsigned char *data);

int cksum_direct_read(int fd, void *buf, size_t size, off_t file_offset);
int cksum_direct_write(int fd, void *buf, size_t size, off_t file_offset);

int checksum_write(struct open_cache_ref *out_ref,
		   PVFS_offset file_offset, 
		   PVFS_size buffer_size,
		   char *data_buf);
int checksum_verify(struct open_cache_ref *out_ref,
		    PVFS_offset file_offset, 
		    PVFS_size buffer_size,
		    char *data_buf);

int crc32_read(struct open_cache_ref *out_ref,
	       PVFS_offset file_offset, 
	       PVFS_size buffer_size,
	       char *data_buf);

/* Error Codes */
#define _CKSUM_EXTENT_EXIST -10
