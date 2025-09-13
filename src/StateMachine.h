#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include "LedController.h"
#include "RVDebug.h"
#include "WCHFlash.h"

// System States
enum SystemState {
    STATE_IDLE,
    STATE_CHECKING_TARGET,
    STATE_PROGRAMMING,
    STATE_CYCLING_FIRMWARE,
    STATE_SUCCESS,
    STATE_ERROR
};

class StateMachine {
public:
    StateMachine(LedController* led, RVDebug* rvd, WCHFlash* flash);
    ~StateMachine();
    
    // State management
    SystemState getCurrentState() const { return current_state; }
    void setState(SystemState state);
    
    // State processing
    void process();
    
    // Actions
    void startProgramming();
    void startTargetCheck();
    void cycleFirmware();
    
    // Configuration
    void setCurrentFirmwareIndex(int index) { current_firmware_index = index; }
    int getCurrentFirmwareIndex() const { return current_firmware_index; }
    
private:
    SystemState current_state;
    uint32_t state_timer;
    int current_firmware_index;
    
    LedController* led_controller;
    RVDebug* rv_debug;
    WCHFlash* wch_flash;
    
    // Helper functions
    bool haltWithTimeout(uint32_t timeout_ms);
    bool programFlash(const uint8_t* data, size_t size);
};

#endif // STATE_MACHINE_H