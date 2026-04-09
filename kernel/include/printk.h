#pragma once
void print_number(long num, int base, int is_signed);
void printk(const char *, ...) __attribute__((format(printf, 1, 2)));
void panic(const char* msg); 
