#include <linux/mm.h>

//
// The following kernel commit changed the get_user_pages_remote()
// function signature and added the inlined get_user_page_vma_remote()
//
// commit ca5e863233e8f6acd1792fd85d6bc2729a1b2c10
// Author: Lorenzo Stoakes <lstoakes@gmail.com>
// Date:   Wed May 17 20:25:39 2023 +0100
//
//    mm/gup: remove vmas parameter from get_user_pages_remote()
//    
//    The only instances of get_user_pages_remote() invocations which used the
//    vmas parameter were for a single page which can instead simply look up the
//    VMA directly. In particular:-
//    
//    - __update_ref_ctr() looked up the VMA but did nothing with it so we simply
//      remove it.
//    
//    - __access_remote_vm() was already using vma_lookup() when the original
//      lookup failed so by doing the lookup directly this also de-duplicates the
//      code.
//    
//    We are able to perform these VMA operations as we already hold the
//    mmap_lock in order to be able to call get_user_pages_remote().
//    
//    As part of this work we add get_user_page_vma_remote() which abstracts the
//    VMA lookup, error handling and decrementing the page reference count should
//    the VMA lookup fail.
//    
//    This forms part of a broader set of patches intended to eliminate the vmas
//    parameter altogether.

struct page *get_user_page_vma_remote_wrapper(struct mm_struct *mm,
                                              unsigned long addr,
                                              unsigned int gup_flags,
                                              struct vm_area_struct **vmaps);

struct page *get_user_page_vma_remote_wrapper(struct mm_struct *mm,
                   unsigned long addr,
                   unsigned int gup_flags,
                   struct vm_area_struct **vmaps)
{
  return get_user_page_vma_remote(mm, addr, gup_flags, vmaps);
}
