// Logging stand-in for the native test env.  The format arguments are never
// evaluated so tests stay quiet and logging can't affect what they check.
#ifndef esp_log_stub_h
#define esp_log_stub_h
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGV(tag, ...) ((void)(tag))
#endif
