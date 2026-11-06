// ServoPort on the board: the SG90 on D26, driven directly with LEDC PWM.
#pragma once

#include "ports.h"

class BoardServo : public core::ServoPort {
public:
    bool begin();  // false if the PWM channel could not be set up
    void command(float angle_deg) override;
};
