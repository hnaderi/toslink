#pragma once
#define ESP_RETURN_ON_FALSE(c, e, t, m) do { if (!(c)) return (e); } while (0)
#define ESP_RETURN_ON_ERROR(x, t, m) do { if ((x) != 0) return (x); } while (0)
