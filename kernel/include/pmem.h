#pragma once

#define PAGE_SIZE 4096UL

void  pmem_init(void *start, void *end);
void *page_alloc(void);
void  page_free(void *page);
