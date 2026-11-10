#pragma once
static inline BaseType_t xTaskCreatePinnedToCore(void (*f)(void *), const char *n, uint32_t s, void *a, UBaseType_t p, void *h, BaseType_t c) { (void)f;(void)n;(void)s;(void)a;(void)p;(void)h;(void)c; return 1; }
