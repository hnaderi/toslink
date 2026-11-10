#pragma once
typedef void *QueueHandle_t;
static inline int xQueueReceive(QueueHandle_t q, void *item, uint32_t t) { (void)q; (void)item; (void)t; return 0; }
