#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pthread.h"
#include "dbpf.h"
#include "gossip.h"

#include "dbpf-checksum.h"
#include "dbpf-checksum-threads.h"

static QLIST_HEAD(cksum_list);
static gen_mutex_t cksum_mutex = GEN_MUTEX_INITIALIZER;
static struct cksum_extent *ext_tail;
static struct s_thread *st;
static int volatile CKSUM_FLAG = 0;

/* Number of threads to assist checksum calculation */
#define MAX_CKSUM_THREAD 4

static int add_new_extent(struct file_cksum_buffer *fcb, 
                          PVFS_offset start, 
                          PVFS_offset end,
                          PVFS_cksum *cksum);
static struct cksum_extent * extent_search(struct file_cksum_buffer *fbuf,
                                           PVFS_offset offset);

static unsigned long int const crctab[256] =
{
   0x00000000,
   0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
   0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6,
   0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
   0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
   0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f,
   0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a,
   0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
   0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58,
   0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
   0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe,
   0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
   0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4,
   0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
   0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5,
   0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
   0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
   0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
   0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
   0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
   0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b,
   0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698,
   0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d,
   0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
   0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f,
   0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
   0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80,
   0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
   0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a,
   0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629,
   0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c,
   0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
   0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
   0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
   0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
   0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
   0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2,
   0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
   0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74,
   0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
   0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21,
   0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
   0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087,
   0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
   0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d,
   0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce,
   0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
   0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
   0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09,
   0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
   0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf,
   0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

/*
 * Function to generate crc32 checksum for a given buffer using buflen as
 * each iteration's length. the buffer can be larger than buflen.
 */
PVFS_cksum crc32_generate(unsigned char *buffer, PVFS_offset buflen)
{
   PVFS_cksum crc = 0;
   unsigned long length = 0;
   size_t bytes_read;
   unsigned char *cp;
   PVFS_size buf_size = CKSUM_BUFLEN;

   cp = buffer;

   while (buf_size>0)
   {
      if (buf_size >= buflen)
      {
	 bytes_read = buflen;
	 buf_size -= buflen;
      }
      else
      {
	 if (buf_size == 0)
	    break;
	 bytes_read = buf_size;
	 buf_size = 0;
      }

      length += bytes_read;
      while (bytes_read--)
	 crc = (crc << 8) ^ crctab[((crc >> 24) ^ *cp++) & 0xFF];
   }

   for (; length; length >>= 8)
      crc = (crc << 8) ^ crctab[((crc >> 24) ^ length) & 0xFF];

   crc = ~crc & 0xFFFFFFFF;

   return crc;
}

static inline void cksum_extent_init(struct cksum_extent *ext, 
                                     PVFS_offset start,
                                     PVFS_offset end,
                                     PVFS_cksum *cksum)
{
   gossip_debug(GOSSIP_CKSUM_DEBUG, "[cksum_extent_init]: Enter.\n");
   ext->start = start;
   ext->end = end;
   if(start == 0 && end == 0)
      ext->cksum = cksum;
   else if(cksum == NULL)
      memset(ext->cksum, '0', BLOCK_SIZE);
   else
      memcpy(ext->cksum, cksum, BLOCK_SIZE);
   gossip_debug(GOSSIP_CKSUM_DEBUG, "[cksum_extent_init]: Exit.\n");
}

/* Initialize the checksum cache. Create thread if required. */
int cksum_init(void)
{
   int i = 0, ret = 0;
   struct cksum_extent *ext;

   /* Not using pre-allocated cache for now */
   return ret;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[cksum_init]: Enter.\n");

   /* Init the extent cache */
   for(i=0; i<CKSUM_BLK_COUNT; i++)
   {
      ext = malloc(sizeof(struct cksum_extent));

      if(!ext)
      {
	 gossip_lerr("[cksum_init]: Error allocating initial checksum extents."
		     " Only %d extents allocated.\n", i);
	 return -1;
      }

      cksum_extent_init(ext, 0, 0, 0);
      qlist_add(&ext->link, &cksum_list);
   }

   /* Assign the last allocated extent as tail. */
   ext_tail = ext;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[cksum_init]: Exit.\n");
   return ret;
}

static void * cksum_thread_function(void *ptr)
{
   int ret = -1;
   struct cksum_op *cks_op = NULL;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[cksum_thread_function]: Enter.\n");

   cks_op = (struct cksum_op *)ptr;

   switch(cks_op->op)
   {
      case CKSUM_READ:
	 ret = checksum_prepare_read(cks_op);
	 break;
      case CKSUM_WRITE:
	 ret = checksum_prepare_write(cks_op);
	 break;
      default:
	 gossip_lerr("[cksum_thread_function]: Invalid checksum operation.\n");
   }

   if(ret == -1)
   {
      gossip_lerr("[cksum_thread_function]: Error with prepare function.\n");
   }

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[cksum_thread_function]: Exit.\n");
   return NULL;
}

/* Read or write operation will call this function. This function just 
 * appends the request into a queue. A thread will then handle the request. 
 * Depending on the request type, a read-ahead will be performed, or CRC 
 * will be calculated.
 */
void * checksum_submit_request(struct open_cache_ref *out_ref, 
                               PVFS_offset file_offset, 
                               PVFS_size buffer_size,
                               char *data_buf, int op)
{
   struct cksum_op *cks_op = NULL;
   int ret = 0;
   void *ptr = NULL;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_submit_request]: Enter.\n");
   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_submit_request]: \n"
		"out_ref    : %p\n"
		"file_offset: %llu\n"
		"buffer_size: %llu\n"
		"data_buf   : %p\n"
		"op         : %d\n", out_ref, llu(file_offset), 
		llu(buffer_size), data_buf, op);

   if(CKSUM_FLAG == 0)
   {
      gen_mutex_lock(&cksum_mutex);

      /* If the flag changed while we were waiting for the lock. */
      if(CKSUM_FLAG == 1)
      {
	 gen_mutex_unlock(&cksum_mutex);
	 goto done;
      }

      cksum_init();

      /* check s_thread status */
      if(!st)
      {
	 st = malloc(sizeof(struct s_thread));
	 ret = s_thread_init(st, MAX_CKSUM_THREAD, cksum_thread_function);
	 if(ret < 0)
	 {
	    gossip_lerr("[checksum_submit_request]: Failed to initialize "
			"checksum thread.\n");
	    return NULL;
	 }
      }
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_submit_request]: Threads "
		   "initialized.\n");
      CKSUM_FLAG = 1;
      gen_mutex_unlock(&cksum_mutex);
   }

  done:
   cks_op = (struct cksum_op *)malloc(sizeof(struct cksum_op));
   cks_op->op = op;
   cks_op->ref = out_ref;
   cks_op->foffset = file_offset;
   cks_op->bsize = buffer_size;
   cks_op->data = data_buf;

   /* NULL now, will be assigned by the thread function. */
   cks_op->cksum = NULL;
   cks_op->count = 0;

   /* Append the request to the queue. */
   ptr = s_thread_work_append(st, cks_op);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_submit_request]: Exit.\n");
   return ptr;
}

/* After completing the read/write operation, the user of checksum module
 * will call this function. This function is blocking function. It will
 * wait for checksum to be read, or computed.
 */
int checksum_complete_request(void *ptr, unsigned char *data)
{
   int ret = -1;
   struct s_thread_work *stw = (struct s_thread_work *)ptr;
   struct cksum_op *cks_op = stw->ptr;

   s_thread_retrieve(st, stw);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_request]: Enter.\n");

   switch(cks_op->op)
   {
      case CKSUM_READ:
	 ret = checksum_verify_read(cks_op, data);
	 break;
      case CKSUM_WRITE:
	 ret = checksum_complete_write(cks_op);
	 break;
      default:
	 gossip_lerr("Invalid checksum complete request.\n");
   }

   free(cks_op);
   free(stw);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_request]: Exit.\n");
   return ret;
}

/* Function to verify wether the read data is correct, or not. If there is
 * a CRC mismatch, an error is thrown. Watch the logs for FATAL error.
*/
int checksum_verify_read(struct cksum_op *cks_op, unsigned char *buf)
{
   int ret = 0, ctr = 0, i = 0;
   struct file_cksum_buffer *fbuf = NULL;
   struct open_cache_ref *ref = NULL;
   PVFS_offset aoffset = 0, foffset = cks_op->foffset, gap = 0;
   PVFS_offset fp = 0, ablkoffset = 0;
   PVFS_size remain = 0, bufcount = 0, read_size = 0;
   PVFS_cksum *cksum = NULL;
   void *tmp_buf = NULL;
   struct cksum_extent *cext = NULL;

   assert(cks_op);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_verify_read]: Enter.\n");

   ref = cks_op->ref;
   cksum = malloc(sizeof(PVFS_cksum)*cks_op->count);

   fbuf = (struct file_cksum_buffer *)ref->cksum_p;
   if(!fbuf)
   {
      /* this should not happen in real life */
      gossip_lerr("FATAL: File pointers lost.\n");
      ret = -1;
      goto out;
   }

   aoffset = ALIGNED_CKSUM_OFFSET(foffset);
   ablkoffset = ALIGNED_CKSUM_BLK_OFFSET(foffset);
   remain = ALIGNED_CKSUM_SIZE(foffset, cks_op->bsize);
   gap = foffset - aoffset;
   bufcount = remain / CKSUM_BUFLEN;

   /* memory allocations */
   tmp_buf = malloc(CKSUM_BUFLEN);
   memset(tmp_buf, '0', CKSUM_BUFLEN);

   /* check if they are same offset. */
   if(aoffset != foffset || cks_op->bsize < CKSUM_BUFLEN)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_verify_read]: "
		   "Checking partial reads\n");
      read_size = dbpf_pread(ref->fd, tmp_buf, CKSUM_BUFLEN, aoffset);

      /* this is necessary for cases where transfer size is less than
       * CRC chunk size. Ex. 4K
       */
      if(cks_op->bsize < CKSUM_BUFLEN)
      {
	 memcpy(tmp_buf+gap, buf, cks_op->bsize);
      }
      else
      {
	 memcpy(tmp_buf+gap, buf, CKSUM_BUFLEN - gap);
      }

      cksum[ctr] = crc32_generate(tmp_buf, CKSUM_BUFLEN);
      ++ctr;
      remain = remain - CKSUM_BUFLEN;
      fp = ablkoffset + CKSUM_BUFLEN - foffset;
   }

   /* Read the whole block and calculate the checksum */
   if(remain >= CKSUM_BUFLEN)
   {
      while(1)
      {
	 cksum[ctr] = crc32_generate(buf + fp, CKSUM_BUFLEN);
	 ++ctr;
	 fp = fp + CKSUM_BUFLEN;
	 remain = remain - CKSUM_BUFLEN;
	 if((cks_op->bsize - fp) < CKSUM_BUFLEN)
	    break;
      }
   }

   if(remain != 0)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_verify_read]: "
		   "Partial tail.\n");
      read_size = dbpf_pread(ref->fd, tmp_buf, CKSUM_BUFLEN, foffset + fp);

      if(read_size < CKSUM_BUFLEN)
      {
	 cksum[ctr] = crc32_generate(tmp_buf, CKSUM_BUFLEN);
	 gossip_debug(GOSSIP_CKSUM_DEBUG, "Partial read %llu\n", 
		      llu(cksum[ctr]));
      }
      else
      {
//	 memcpy(tmp_buf, buf + fp, cks_op->bsize - fp);
	 cksum[ctr] = crc32_generate(tmp_buf, CKSUM_BUFLEN);
      }
      ++ctr;
   }

   /* free the temp buffer */
   free(tmp_buf);

   /* find the checksum in cache. It should be available - submit_request()
    * must have found it and added to cache.
    */
   cext = extent_search(fbuf, cks_op->foffset);
   if(!cext)
   {
      gossip_err("[checksum_verify_read]: FATAL: Cache not updated (off: "
		 " %llu).\n", llu(cks_op->foffset));
      ret = -1;
      goto out;
   }

   /* Now verify all the CRCs. If mismatch, throw error. */
   for(i=0; i<ctr; i++)
   {
      PVFS_offset loc = (cks_op->foffset - cext->start) / CKSUM_BUFLEN;
      if(cksum[i] != cext->cksum[loc + i])
      {
	 gossip_err("[checksum_verify_read]: FATAL: Checksum error "
		    "%llu/%llu/%llu %d-%llu:%llu; %d\n", 
		    llu(cks_op->foffset), llu(cks_op->foffset+i*CKSUM_BUFLEN), 
		    llu(cks_op->bsize), i, 
		    llu(cksum[i]), llu(cks_op->cksum[i]), ctr);
	 ret = -1;
	 goto out;
      }
   }

  out:
   gossip_debug(GOSSIP_CKSUM_DEBUG,"[checksum_verify_read]: Exit\n");
   return ret;
}

/*
 * This function will write the checksum to disk. It is part of the 
 * complete_request operation.
 */
int checksum_complete_write(struct cksum_op *cks_op)
{
   PVFS_offset ckoffset = 0;
   PVFS_offset a_blk_offset = 0;
   int count;
   struct file_cksum_buffer *fbuf = NULL;
   struct cksum_extent *cext = NULL;
   PVFS_offset fp = 0;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_write]: Enter.\n");

   a_blk_offset = ALIGNED_CKSUM_BLK_OFFSET(cks_op->foffset);
   a_blk_offset = a_blk_offset / CKSUM_BLK_BUFLEN;

   ckoffset = (a_blk_offset * BLOCK_SIZE);

   count = cks_op->count;

   /* find the offset that you have to fill and update it. find it in cache. */
   fbuf = (struct file_cksum_buffer *)cks_op->ref->cksum_p;
   if(!fbuf)
   {
      gossip_lerr("FATAL. file buffer lost.\n");
      return -1;
   }

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_write]: Finding "
		"extent in cache.\n");

   /* See if you can find the extent for this in cache. */
   cext = extent_search(fbuf, cks_op->foffset);
   if(cext)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_write]: Found "
		   "extent in cache %llu, size %llu\n", llu(cks_op->foffset),
		   llu(cks_op->bsize));

      fp = (cks_op->foffset - cext->start) / CKSUM_BUFLEN;

      if(cks_op->foffset + cks_op->bsize > (cext->end+1))
      {
	 /* Copy checksum belonging to this block. */
	 unsigned int copy = CKSUM_BLK_COUNT - fp;
	 PVFS_offset next = cext->end+1;
	 memcpy(cext->cksum + fp, cks_op->cksum, copy*sizeof(PVFS_cksum));
	 cksum_direct_write(cks_op->ref->cks_fd, cext->cksum,
			    BLOCK_SIZE, 
			    BLOCK_SIZE*(cext->start/CKSUM_BLK_BUFLEN));

	 /* Get the next block */
	 cext = extent_search(fbuf, next);
	 if(!cext)
	 {
	    /* Throw alarm */
	    gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_write]: "
			 "ALARM\n");
	 }
	 else
	 {
	    memcpy(cext->cksum, cks_op->cksum + copy, 
		   (cks_op->count-copy)*sizeof(PVFS_cksum));
	    cksum_direct_write(cks_op->ref->cks_fd, cext->cksum,
			       BLOCK_SIZE,
			       BLOCK_SIZE*(cext->start/CKSUM_BLK_BUFLEN));
	 }
      }
      else
      {
	 /* Copy all checksums. */
	 memcpy(cext->cksum + fp, cks_op->cksum, 
		cks_op->count*sizeof(PVFS_cksum));
	 cksum_direct_write(cks_op->ref->cks_fd, cext->cksum,
			    BLOCK_SIZE, 
			    BLOCK_SIZE*(cext->start/CKSUM_BLK_BUFLEN));
      }
   }
   else
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_write]: FATAL: "
		   "extent block not in cache.\n");
   }

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_complete_write]: Exit.\n");
   return 0;
}

/*!
 * function: add_new_extent()
 *
 * This function will add a new extent for a file into cache. The function
 * adds full checksum extents only. Changes to part of checksum block will
 * be done by the function using that block. That checksum block would have
 * been added here before the change was made.
 */
static int add_new_extent(struct file_cksum_buffer *fcb, 
                          PVFS_offset start, 
                          PVFS_offset end,
                          PVFS_cksum *cksum)
{
   struct rb_node *node = NULL;
   struct rb_node **new = &fcb->extents.rb_node;
   struct rb_node *parent = NULL;
   struct cksum_extent *this = NULL;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[add_new_extent]: Enter.\n");

   while(*new)
   {
      parent = *new;
      this = rb_entry(parent, struct cksum_extent, node);

      if(start < this->start)
      {
	 new = &(*new)->rb_left;
      }
      else if(start > this->end)
      {
	 new = &(*new)->rb_right;
      }
      else
      {
	 gossip_debug(GOSSIP_CKSUM_DEBUG, "[add_new_extent]: XXX: Extent "
		      "exists\n");
	 return _CKSUM_EXTENT_EXIST;
      }
   }

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[add_new_extent]: Adding new extent - "
		"start(%llu), end(%llu) this(%p) cksum(%p)\n", llu(start), 
		llu(end), this, cksum);

   /* Allocate memory to store 1 block of checksum */
   this = NULL;
   this = malloc(sizeof(struct cksum_extent));
   this->cksum = malloc(BLOCK_SIZE);

   cksum_extent_init(this, start, end-1, cksum);

   /* Add the new node and rebalance */
   gossip_debug(GOSSIP_CKSUM_DEBUG, "[add_new_extent]: Rebalancing\n");
   node = &this->node;
   rb_link_node(node, parent, new);
   rb_insert_color(node, &fcb->extents);

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[add_new_extent]: Exit.\n");
   return 0;
}

/*!
 * Search for extent in cache. If any part of the request is missing, this
 * function will return NULL. This is because it will not add any cost to 
 * re-read checksum. Data read size ranges between 64K-256K. Checksums are
 * read from disk in 4K (equivalent of 32MB of data). If something is not in 
 * cache, you are going to get it from the disk with a single I/O.
 *
 * \param fbuf file buffer
 * \param start Aligned offset start
 * \param end aligned offset end
 * \return checksum extent
 */
static struct cksum_extent * extent_search(struct file_cksum_buffer *fbuf,
                                           PVFS_offset start)
{
   struct rb_node *n = fbuf->extents.rb_node;
   struct cksum_extent *extent = NULL;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[extent_search]: Enter (%llu).\n",
		llu(start));

   while(n)
   {
      extent = rb_entry(n, struct cksum_extent, node);
      
      if(start < extent->start)
	 n = n->rb_left;
      else if(start > extent->end)
	 n = n->rb_right;
      else
      {
	 gossip_debug(GOSSIP_CKSUM_DEBUG, "[extent_search]: Exit (%llu).\n",
		      llu(extent->start));
	 return extent;
      }
   }

   if(!n)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[extent_search]: Exit (NULL).\n");
      return NULL;
   }

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[extent_search]: Exit.\n");
   return NULL;

   /* Don't think we will ever hit this. */
   if(extent->start > start)
   {
      n = rb_prev(&extent->node);
      extent = rb_entry(n, struct cksum_extent, node);
   }

   return extent;
}

/* Prepare for the read operation. Read-ahead the CRCs. */
int checksum_prepare_read(struct cksum_op *cks_op)
{
   int ret = 0, i=0;
   struct open_cache_ref *ref = NULL;
   struct file_cksum_buffer *fbuf = NULL;
   struct cksum_extent *cext = NULL;
   PVFS_offset bufcount = 0, ckoffset = 0, aoffset = 0;
   PVFS_cksum *cksum = NULL, *cksumblk = NULL;	 
   PVFS_offset lcount = 0;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_read]: Enter.\n");

   ref = cks_op->ref;

   fbuf = (struct file_cksum_buffer *)ref->cksum_p;
   if(!fbuf)
   {
      fbuf = malloc(sizeof(struct file_cksum_buffer));
      fbuf->count = 0;
      fbuf->extents = RB_ROOT;
      ref->cksum_p = (void *)fbuf;
   }

   bufcount = (ALIGNED_CKSUM_BLK_OFFSET((cks_op->foffset + cks_op->bsize)) +
	       CKSUM_BLK_BUFLEN - ALIGNED_CKSUM_BLK_OFFSET(cks_op->foffset));
   bufcount = bufcount / CKSUM_BUFLEN;
   aoffset = ALIGNED_CKSUM_BLK_OFFSET(cks_op->foffset);// / CKSUM_BLK_BUFLEN;
   ckoffset = aoffset * CKSUM_BLK_BUFLEN;

   cksum = malloc(sizeof(PVFS_cksum)*bufcount);
   cksumblk = malloc(BLOCK_SIZE);

   /* update the checksum count before we change its value. */
   cks_op->count = bufcount;

   /* check if the checksum is available in cache */
   while(bufcount > 0)
   {
      cext = extent_search(fbuf, aoffset);
      if(cext)
      {
	 gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_read]: "
		      "Checksum found in cache.\n");
	 bufcount -= CKSUM_BLK_COUNT;
	 aoffset += CKSUM_BLK_BUFLEN;
	 ckoffset = (cks_op->foffset + 
		     (i*CKSUM_BLK_BUFLEN) - cext->start)/CKSUM_BUFLEN;
	 lcount = (cks_op->foffset + (i*CKSUM_BLK_BUFLEN) - 
		   cext->start) % CKSUM_BLK_COUNT;
	 memcpy(cksum + ckoffset, cext->cksum, lcount*sizeof(PVFS_cksum));
      }
      else
	 break;
      ++i;
   }

   if(bufcount > 0)
   {
      ckoffset = ALIGNED_CKSUM_BLK_OFFSET((cks_op->foffset + 
					   i*CKSUM_BLK_BUFLEN));
      cksum_direct_read(ref->cks_fd, cksumblk, BLOCK_SIZE, 
			BLOCK_SIZE*(ckoffset/CKSUM_BLK_BUFLEN));

      /* Add it to checksum cache */
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_read]: Added to "
		   "cache (start: %llu, end: %llu\n", llu(ckoffset), 
		   llu(ckoffset + CKSUM_BLK_BUFLEN));
      add_new_extent(ref->cksum_p, ckoffset, ckoffset + CKSUM_BLK_BUFLEN,
		     cksumblk);

      if(i!=0)
      {
	 /* Update cksum */
	 PVFS_offset loff = (cks_op->foffset + cks_op->bsize - 
			     ckoffset)/CKSUM_BUFLEN;
	 memcpy(cksum + lcount, cksumblk, loff*sizeof(PVFS_cksum));
      }
      else
      {
	 /* Update cksum (i==0) */
	 PVFS_offset loff = (cks_op->foffset - ckoffset)/CKSUM_BUFLEN;
	 PVFS_offset many = (ckoffset + CKSUM_BLK_BUFLEN - 
			     cks_op->foffset)/CKSUM_BUFLEN;
	 memcpy(cksum, cksumblk+loff, many*sizeof(PVFS_cksum));

	 /* Read the next checksum block if required. */
	 if(cks_op->foffset + cks_op->bsize > ckoffset + CKSUM_BLK_BUFLEN)
	 {
	    /* Read the next block and add it to cache. */
	    cksum_direct_read(ref->cks_fd, cksumblk, BLOCK_SIZE, 
			      BLOCK_SIZE*(ckoffset/CKSUM_BLK_BUFLEN) 
			      + BLOCK_SIZE);
	    add_new_extent(ref->cksum_p, ckoffset + CKSUM_BLK_BUFLEN,
			   ckoffset + 2*CKSUM_BLK_BUFLEN, cksumblk);

	    /* Update cksum */
	    loff = (cks_op->foffset + cks_op->bsize - (
		       ckoffset + CKSUM_BLK_BUFLEN))/CKSUM_BUFLEN;
	    memcpy(cksum+many, cksumblk, loff*sizeof(PVFS_cksum));
	 }
      }
   }

   cks_op->cksum = cksum;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_read]: Exit.\n");
   return ret;
}

/* Prepare for the write operation. Calculate the CRCs */
int checksum_prepare_write(struct cksum_op *cks_op)
{
   int ctr = 0;
   struct open_cache_ref *ref = cks_op->ref;
   PVFS_offset foff = cks_op->foffset;
   PVFS_size bsize = cks_op->bsize;
   struct file_cksum_buffer *fbuf = NULL;
   PVFS_offset aoffset = 0, fp = 0;
   PVFS_size read_size = 0, remain = 0, bufcount = 0, s_gap = 0;
   PVFS_cksum *cksum = NULL;
   unsigned char *tmp_buf = NULL;
   struct cksum_extent *cache_ext = NULL;
   void *temp = NULL;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_write]: Enter.\n");

   /* Get the file buffer. */
   fbuf = (struct file_cksum_buffer *)ref->cksum_p;
   if(!fbuf)
   {
      /* this file is being accessed for the first time in this run. */
      fbuf = malloc(sizeof(struct file_cksum_buffer));
      fbuf->count = 0;
      fbuf->extents = RB_ROOT;
      ref->cksum_p = (void *)fbuf;
   }

   aoffset = ALIGNED_CKSUM_OFFSET(foff);
   remain = ALIGNED_CKSUM_SIZE(foff, bsize);
   s_gap = foff - aoffset;

   bufcount = remain / CKSUM_BUFLEN;
   cksum = malloc(sizeof(PVFS_cksum) * bufcount);

   tmp_buf = malloc(CKSUM_BUFLEN);
   memset(tmp_buf, '0', CKSUM_BUFLEN);

   /* Check if this is an unaligned request. */
   if(aoffset != foff)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_write]: "
		   "Unaligned head. gap - %llu, foff - %llu,  off - %llu\n", 
		   llu(s_gap), llu(foff), llu(aoffset));
      read_size = dbpf_pread(ref->fd, tmp_buf, CKSUM_BUFLEN, aoffset);

      /* File is empty at this offset */
      if(!(tmp_buf+CKSUM_BUFLEN))
      {
	 temp = realloc(tmp_buf, CKSUM_BUFLEN);
      }

      memcpy(tmp_buf + s_gap, cks_op->data, CKSUM_BUFLEN - s_gap);
      cksum[ctr] = crc32_generate(tmp_buf, CKSUM_BUFLEN);
      ++ctr;
      remain = remain - CKSUM_BUFLEN;
      fp = CKSUM_BUFLEN - foff;
   }

   if(remain >= CKSUM_BUFLEN)
   {
      while(1)
      {
	 memcpy(tmp_buf, (cks_op->data + fp), CKSUM_BUFLEN);
	 cksum[ctr] = crc32_generate(tmp_buf, CKSUM_BUFLEN);

	 ++ctr;
	 remain = remain - CKSUM_BUFLEN;
	 fp = fp + CKSUM_BUFLEN;
	 if((bsize - fp) < CKSUM_BUFLEN)
	    break;
      }
   }

   if(remain != 0)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_write]: "
		   "Unaligned tail.\n");
      memset(tmp_buf, '0', CKSUM_BUFLEN);
      read_size = dbpf_pread(ref->fd, tmp_buf, CKSUM_BUFLEN, foff + fp);
      memcpy(tmp_buf, cks_op->data + fp, bsize - fp);
      cksum[ctr] = crc32_generate(tmp_buf, CKSUM_BUFLEN);
      ++ctr;
   }

   /* Check if checksum for this is available in cache, if not, add it */
   aoffset = ALIGNED_CKSUM_BLK_OFFSET(cks_op->foffset);
   cache_ext = extent_search(fbuf, cks_op->foffset);

   if(!cache_ext)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_write]: Reading "
		   "from drive for %llu\n", llu(aoffset));
      /* Read from drive and add it to cache. */
      cache_ext = malloc(sizeof(struct cksum_extent));
      cache_ext->cksum = malloc(BLOCK_SIZE);
      cksum_direct_read(ref->cks_fd, cache_ext->cksum, BLOCK_SIZE,
			BLOCK_SIZE*(aoffset/CKSUM_BLK_BUFLEN));
      add_new_extent(fbuf, aoffset, aoffset + CKSUM_BLK_BUFLEN, 
		     cache_ext->cksum);
      free(cache_ext);
   }

   /* Read the next extent block also is required. */
   if(cks_op->foffset + bsize > aoffset + CKSUM_BLK_BUFLEN)
   {
      gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_write]: More "
		   "reading from drive for %llu\n", llu(aoffset));
      /* check cache */
      cache_ext = extent_search(fbuf, aoffset + CKSUM_BLK_BUFLEN);

      /* read the next block also */
      if(!cache_ext)
      {
	 cache_ext = malloc(sizeof(struct cksum_extent));
	 cache_ext->cksum = malloc(BLOCK_SIZE);
	 cksum_direct_read(ref->cks_fd, cache_ext->cksum, BLOCK_SIZE,
			   BLOCK_SIZE*((aoffset +
					CKSUM_BLK_BUFLEN)/CKSUM_BLK_BUFLEN));
	 add_new_extent(fbuf, aoffset + CKSUM_BLK_BUFLEN, 
			aoffset + 2*CKSUM_BLK_BUFLEN, cache_ext->cksum);
	 free(cache_ext);
      }
   }

   /* Free it now, we don't need it no more. */
   free(tmp_buf);

   cks_op->cksum = cksum;
   cks_op->count = ctr;

   gossip_debug(GOSSIP_CKSUM_DEBUG, "[checksum_prepare_write]: Exit.\n");
   return 0;
}

