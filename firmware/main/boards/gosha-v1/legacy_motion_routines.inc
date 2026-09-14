// Generated from otto_movements.cc by import_legacy_motion_routines.py.
// Original amplitudes, periods, phases and postures; no RTOS/hardware calls.
// Only the right arm is available. See LEGACY_MOVEMENTS_RESTORE_RU.md.

void LegacyMotionPlan::Home(bool hands_down) {
    if (is_otto_resting_ == false) {

        int homes[SERVO_COUNT];
        for (int i = 0; i < SERVO_COUNT; i++) {
            if (i == LEFT_HAND || i == RIGHT_HAND) {
                if (hands_down) {

                    if (i == LEFT_HAND) {
                        homes[i] = HAND_HOME_POSITION;
                    } else {
                        homes[i] = 180 - HAND_HOME_POSITION;
                    }
                } else {

                    homes[i] = Position(i);
                }
            } else {

                homes[i] = 90;
            }
        }

        MoveServos(700, homes);
        is_otto_resting_ = true;
    }

    Hold(200);
}

void LegacyMotionPlan::Jump(float steps, int period) {
    (void)steps;
    int up[SERVO_COUNT] = {90, 90, 150, 30, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    MoveServos(period, up);
    int down[SERVO_COUNT] = {90, 90, 90, 90, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    MoveServos(period, down);
}

void LegacyMotionPlan::Walk(float steps, int period, int dir, int amount) {

    int A[SERVO_COUNT] = {30, 30, 30, 30, 0, 0};
    int O[SERVO_COUNT] = {0, 0, 5, -5, HAND_HOME_POSITION - 90, HAND_HOME_POSITION};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(dir * -90), DEG2RAD(dir * -90), 0, 0};

    if (amount > 0 && right_arm_available_) {

        A[LEFT_HAND] = amount;
        A[RIGHT_HAND] = amount;

        phase_diff[LEFT_HAND] = phase_diff[RIGHT_LEG];
        phase_diff[RIGHT_HAND] = phase_diff[LEFT_LEG];
    } else {
        A[LEFT_HAND] = 0;
        A[RIGHT_HAND] = 0;
    }

    Execute(A, O, period, phase_diff, steps);
}

void LegacyMotionPlan::Turn(float steps, int period, int dir, int amount) {

    int A[SERVO_COUNT] = {30, 30, 30, 30, 0, 0};
    int O[SERVO_COUNT] = {0, 0, 5, -5, HAND_HOME_POSITION - 90, HAND_HOME_POSITION};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(-90), 0, 0};

    if (dir == LEFT) {
        A[0] = 30;
        A[1] = 0;
    } else {
        A[0] = 0;
        A[1] = 30;
    }

    if (amount > 0 && right_arm_available_) {

        A[LEFT_HAND] = amount;
        A[RIGHT_HAND] = amount;

        phase_diff[LEFT_HAND] = phase_diff[LEFT_LEG];
        phase_diff[RIGHT_HAND] = phase_diff[RIGHT_LEG];
    } else {
        A[LEFT_HAND] = 0;
        A[RIGHT_HAND] = 0;
    }

    Execute(A, O, period, phase_diff, steps);
}

void LegacyMotionPlan::Bend(int steps, int period, int dir) {

    int bend1[SERVO_COUNT] = {90, 90, 62, 35, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    int bend2[SERVO_COUNT] = {90, 90, 62, 105, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    int homes[SERVO_COUNT] = {90, 90, 90, 90, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};

    if (dir == -1) {
        bend1[2] = 180 - 35;
        bend1[3] = 180 - 60;
        bend2[2] = 180 - 105;
        bend2[3] = 180 - 60;
    }

    int T2 = 800;

    for (int i = 0; i < steps; i++) {
        MoveServos(T2 / 2, bend1);
        MoveServos(T2 / 2, bend2);
        Hold(period * 0.8);
        MoveServos(500, homes);
    }
}

void LegacyMotionPlan::ShakeLeg(int steps, int period, int dir) {

    int numberLegMoves = 2;

    int shake_leg1[SERVO_COUNT] = {90, 90, 58, 35, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    int shake_leg2[SERVO_COUNT] = {90, 90, 58, 120, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    int shake_leg3[SERVO_COUNT] = {90, 90, 58, 60, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};
    int homes[SERVO_COUNT] = {90, 90, 90, 90, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};

    if (dir == LEFT) {
        shake_leg1[2] = 180 - 35;
        shake_leg1[3] = 180 - 58;
        shake_leg2[2] = 180 - 120;
        shake_leg2[3] = 180 - 58;
        shake_leg3[2] = 180 - 60;
        shake_leg3[3] = 180 - 58;
    }

    int T2 = 1000;

    period = period - T2;
    period = std::max(period, 200 * numberLegMoves);

    for (int j = 0; j < steps; j++) {

        MoveServos(T2 / 2, shake_leg1);
        MoveServos(T2 / 2, shake_leg2);

        for (int i = 0; i < numberLegMoves; i++) {
            MoveServos(period / (2 * numberLegMoves), shake_leg3);
            MoveServos(period / (2 * numberLegMoves), shake_leg2);
        }
        MoveServos(500, homes);
    }

    Hold(period);
}

void LegacyMotionPlan::Sit() {
    int target[SERVO_COUNT] = {120, 60, 0, 180, 45, 135};
    MoveServos(600, target);
}

void LegacyMotionPlan::UpDown(float steps, int period, int height) {

    int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
    int O[SERVO_COUNT] = {0, 0, height, -height, HAND_HOME_POSITION - 90, 90 - HAND_HOME_POSITION};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(90), 0, 0};

    Execute(A, O, period, phase_diff, steps);
}

void LegacyMotionPlan::Swing(float steps, int period, int height) {

    int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
    int O[SERVO_COUNT] = {
        0, 0, height / 2, -height / 2, HAND_HOME_POSITION - 90, 90 - HAND_HOME_POSITION};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(0), DEG2RAD(0), 0, 0};

    Execute(A, O, period, phase_diff, steps);
}

void LegacyMotionPlan::Moonwalker(float steps, int period, int height, int dir) {

    int A[SERVO_COUNT] = {0, 0, height, height, 0, 0};
    int O[SERVO_COUNT] = {
        0, 0, height / 2 + 2, -height / 2 - 2, HAND_HOME_POSITION - 90, 90 - HAND_HOME_POSITION};
    int phi = -dir * 90;
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(phi), DEG2RAD(-60 * dir + phi), 0, 0};

    Execute(A, O, period, phase_diff, steps);
}

void LegacyMotionPlan::WhirlwindLeg(float steps, int period, int amplitude) {

    int target[SERVO_COUNT] = {90, 90, 180, 90, 45, 20};
    MoveServos(100, target);
    target[RIGHT_FOOT] = 160;
    MoveServos(500, target);
    Hold(1000);

    int C[SERVO_COUNT] = {90, 90, 180, 160, 45, 20};
    int A[SERVO_COUNT] = {amplitude, 0, 0, 0, amplitude, 0};
    double phase_diff[SERVO_COUNT] = {DEG2RAD(20), 0, 0, 0, DEG2RAD(20), 0};
    Execute2(A, C, period, phase_diff, steps);

}

void LegacyMotionPlan::HandsUp(int period, int dir) {

    int target[SERVO_COUNT] = {90, 90, 90, 90, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};

    if (dir == 0) {
        target[LEFT_HAND] = 170;
        target[RIGHT_HAND] = 10;
    } else if (dir == LEFT) {
        target[LEFT_HAND] = 170;
        target[RIGHT_HAND] = Position(RIGHT_HAND);
    } else if (dir == RIGHT) {
        target[RIGHT_HAND] = 10;
        target[LEFT_HAND] = Position(LEFT_HAND);
    }

    MoveServos(period, target);
}

void LegacyMotionPlan::HandsDown(int period, int dir) {

    int target[SERVO_COUNT] = {90, 90, 90, 90, HAND_HOME_POSITION, 180 - HAND_HOME_POSITION};

    if (dir == LEFT) {
        target[RIGHT_HAND] = Position(RIGHT_HAND);
    } else if (dir == RIGHT) {
        target[LEFT_HAND] = Position(LEFT_HAND);
    }

    MoveServos(period, target);
}

void LegacyMotionPlan::HandWave(int dir) {

    if (dir == LEFT) {
        int center_angle[SERVO_COUNT] = {90, 90, 90, 90, 160, 135};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 20, 0};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), 0};
        Execute2(A, center_angle, 300, phase_diff, 5);
    }
    else if (dir == RIGHT) {
        int center_angle[SERVO_COUNT] = {90, 90, 90, 90, 45, 20};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 0, 20};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, 0, DEG2RAD(90)};
        Execute2(A, center_angle, 300, phase_diff, 5);
    }
    else {
        int center_angle[SERVO_COUNT] = {90, 90, 90, 90, 160, 20};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 20, 20};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), DEG2RAD(90)};
        Execute2(A, center_angle, 300, phase_diff, 5);
    }
}

void LegacyMotionPlan::Windmill(float steps, int period, int amplitude) {

    int center_angle[SERVO_COUNT] = {90, 90, 90, 90, 90, 90};
    int A[SERVO_COUNT] = {0, 0, 0, 0, amplitude, amplitude};
    double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), DEG2RAD(90)};
    Execute2(A, center_angle, period, phase_diff, steps);
}

void LegacyMotionPlan::Takeoff(float steps, int period, int amplitude) {

    Home(true);

    int center_angle[SERVO_COUNT] = {90, 90, 90, 90, 90, 90};
    int A[SERVO_COUNT] = {0, 0, 0, 0, amplitude, amplitude};
    double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), DEG2RAD(-90)};
    Execute2(A, center_angle, period, phase_diff, steps);
}

void LegacyMotionPlan::Fitness(float steps, int period, int amplitude) {

    int target[SERVO_COUNT] = {90, 90, 90, 0, 160, 135};
    MoveServos(100, target);
    target[LEFT_FOOT] = 20;
    MoveServos(400, target);
    Hold(2000);

    int C[SERVO_COUNT] = {90, 90, 20, 90, 160, 135};
    int A[SERVO_COUNT] = {0, 0, 0, 0, 0, amplitude};
    double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
    Execute2(A, C, period, phase_diff, steps);

}

void LegacyMotionPlan::Greeting(int dir, float steps) {

    if (dir == LEFT) {
        int target[SERVO_COUNT] = {90, 90, 150, 150, 45, 135};
        MoveServos(400, target);
        int C[SERVO_COUNT] = {90, 90, 150, 150, 160, 135};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 20, 0};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
        Execute2(A, C, 300, phase_diff, steps);
    }
    else if (dir == RIGHT) {
        int target[SERVO_COUNT] = {90, 90, 30, 30, 45, 135};
        MoveServos(400, target);
        int C[SERVO_COUNT] = {90, 90, 30, 30, 45, 20};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 0, 20};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
        Execute2(A, C, 300, phase_diff, steps);
    }

}

void LegacyMotionPlan::Shy(int dir, float steps) {

    if (dir == LEFT) {
        int target[SERVO_COUNT] = {90, 90, 150, 150, 45, 135};
        MoveServos(400, target);
        int C[SERVO_COUNT] = {90, 90, 150, 150, 45, 135};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 20, 20};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), DEG2RAD(-90)};
        Execute2(A, C, 300, phase_diff, steps);
    }
    else if (dir == RIGHT) {
        int target[SERVO_COUNT] = {90, 90, 30, 30, 45, 135};
        MoveServos(400, target);
        int C[SERVO_COUNT] = {90, 90, 30, 30, 45, 135};
        int A[SERVO_COUNT] = {0, 0, 0, 0, 0, 20};
        double phase_diff[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), DEG2RAD(-90)};
        Execute2(A, C, 300, phase_diff, steps);
    }
}

void LegacyMotionPlan::RadioCalisthenics() {

    const int period = 1000;
    const float steps = 8.0;

    int C1[SERVO_COUNT] = {90, 90, 90, 90, 145, 45};
    int A1[SERVO_COUNT] = {0, 0, 0, 0, 45, 45};
    double phase_diff1[SERVO_COUNT] = {0, 0, 0, 0, DEG2RAD(90), DEG2RAD(-90)};
    Execute2(A1, C1, period, phase_diff1, steps);

    int C2[SERVO_COUNT] = {90, 90, 115, 65, 90, 90};
    int A2[SERVO_COUNT] = {0, 0, 25, 25, 0, 0};
    double phase_diff2[SERVO_COUNT] = {0, 0, DEG2RAD(90), DEG2RAD(-90), 0, 0};
    Execute2(A2, C2, period, phase_diff2, steps);

    int C3[SERVO_COUNT] = {90, 90, 130, 130, 90, 90};
    int A3[SERVO_COUNT] = {0, 0, 0, 0, 20, 0};
    double phase_diff3[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
    Execute2(A3, C3, period, phase_diff3, steps);

    int C4[SERVO_COUNT] = {90, 90, 50, 50, 90, 90};
    int A4[SERVO_COUNT] = {0, 0, 0, 0, 0, 20};
    double phase_diff4[SERVO_COUNT] = {0, 0, 0, 0, 0, 0};
    Execute2(A4, C4, period, phase_diff4, steps);
}

void LegacyMotionPlan::MagicCircle() {

    int A[SERVO_COUNT] = {30, 30, 30, 30, 50, 50};
    int O[SERVO_COUNT] = {0, 0, 5, -5, 0, 0};
    double phase_diff[SERVO_COUNT] = {0, 0, DEG2RAD(-90), DEG2RAD(-90), DEG2RAD(-90) , DEG2RAD(90)};

    Execute(A, O, 700, phase_diff, 40);
}

void LegacyMotionPlan::Showcase() {
    if (GetRestState() == true) {
        SetRestState(false);
    }

    Walk(3, 1000, FORWARD, 50);
    Hold(500);

    if (right_arm_available_) {
        HandWave(RIGHT);
        Hold(500);
    }

    if (right_arm_available_) {
        RadioCalisthenics();
        Hold(500);
    }

    Moonwalker(3, 900, 25, LEFT);
    Hold(500);

    Swing(3, 1000, 30);
    Hold(500);

    if (right_arm_available_) {
        Takeoff(5, 300, 40);
        Hold(500);
    }

    if (right_arm_available_) {
        Fitness(5, 1000, 25);
        Hold(500);
    }

    Walk(3, 1000, BACKWARD, 50);
}
