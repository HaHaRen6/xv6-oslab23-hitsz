// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13
struct {
  struct spinlock total_lock;
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf hashbucket[NBUCKETS];//每个哈希队列一个linked list及一个lock
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.total_lock, "bcache");

  // Create linked list of buffers
  for(int i = 0; i < NBUCKETS; i++){
    // snprintf(name, sizeof(name), "%s%d", "bcache", i);
    initlock(&bcache.lock[i], "bcache");
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }
  
  // int cnt = 0;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.hashbucket[b->blockno % NBUCKETS].next;
    b->prev = &bcache.hashbucket[b->blockno % NBUCKETS];
    
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[b->blockno % NBUCKETS].next->prev = b;
    bcache.hashbucket[b->blockno % NBUCKETS].next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int index = blockno % NBUCKETS;
  acquire(&bcache.lock[index]);

  // Is the block already cached?
  for(b = bcache.hashbucket[index].next; b != &bcache.hashbucket[index]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      // 指导书: bcache.lock是自旋锁，用于表示 bcache 链表是否被锁住。b->lock是睡眠锁，用于表示缓存数据块buf是否被锁住。
      release(&bcache.lock[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache.hashbucket[index].prev; b != &bcache.hashbucket[index]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock[index]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 防止死锁
  release(&bcache.lock[index]);
  acquire(&bcache.total_lock);
  acquire(&bcache.lock[index]);

  // 当bget()查找数据块未命中时，bget()可从其他哈希桶选择一个未被使用的缓存块
  for (int i = (index + 1) % NBUCKETS; i != index; i = (i + 1) % NBUCKETS) {
    if (i != index){
      acquire(&bcache.lock[i]);
      for(b = bcache.hashbucket[i].prev; b != &bcache.hashbucket[i]; b = b->prev){
        if(b->refcnt == 0) {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          b->next->prev = b->prev;
          b->prev->next = b->next;
          release(&bcache.lock[i]);
          // 移入到当前的哈希桶链表中使用。
          b->next = bcache.hashbucket[index].next;
          b->prev = &bcache.hashbucket[index];
          bcache.hashbucket[index].next->prev = b;
          bcache.hashbucket[index].next = b;
          release(&bcache.lock[index]);
          release(&bcache.total_lock);
          acquiresleep(&b->lock);
          return b;
        }
      }
      release(&bcache.lock[i]);
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int index = b->blockno % NBUCKETS;
  acquire(&bcache.lock[index]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[index].next;
    b->prev = &bcache.hashbucket[index];
    bcache.hashbucket[index].next->prev = b;
    bcache.hashbucket[index].next = b;
  }
  release(&bcache.lock[index]);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock[b->blockno % NBUCKETS]);
  b->refcnt++;
  release(&bcache.lock[b->blockno % NBUCKETS]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock[b->blockno % NBUCKETS]);
  b->refcnt--;
  release(&bcache.lock[b->blockno % NBUCKETS]);
}


