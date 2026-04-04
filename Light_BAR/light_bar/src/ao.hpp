#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstring>

#include "event.hpp"

/*
 * Base Active Object.
 *
 * Concrete AOs inherit from this. The queue is the only public interface —
 * callers post events, never touch subclass data directly.
 *
 * State is a pointer-to-member-function of the concrete AO subclass.
 * We keep it in the subclass (not here) so the base stays type-agnostic.
 * The task loop calls dispatch(), which the subclass implements.
 */
class AO {
public:
    explicit AO(uint32_t depth, uint32_t evtSize)
        : queue_(xQueueCreate(depth, evtSize)) { configASSERT(queue_); }
    virtual ~AO() = default;
 
    void start(const char *name, uint32_t stack, UBaseType_t prio) {
        xTaskCreate(entry, name, stack, this, prio, &task_);
    }
    BaseType_t post(const Event &e, TickType_t t = portMAX_DELAY) {
        return xQueueSend(queue_, &e, t);
    }
    BaseType_t postFromISR(const Event &e, BaseType_t *pw = nullptr) {
        return xQueueSendFromISR(queue_, &e, pw);
    }
 
protected:
    QueueHandle_t queue() const { return queue_; }  // ← subclass can read it
    virtual void run() = 0;
 
private:
    static void entry(void *p) { static_cast<AO *>(p)->run(); }
    QueueHandle_t queue_;
    TaskHandle_t  task_ = nullptr;
};
