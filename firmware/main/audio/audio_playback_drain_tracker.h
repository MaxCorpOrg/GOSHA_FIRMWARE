#ifndef AUDIO_PLAYBACK_DRAIN_TRACKER_H
#define AUDIO_PLAYBACK_DRAIN_TRACKER_H

#include <cstdint>

class AudioPlaybackDrainTracker {
public:
    uint32_t BeginDecode() {
        decode_in_flight_++;
        return generation_;
    }

    void FinishDecode() {
        if (decode_in_flight_ > 0) {
            decode_in_flight_--;
        }
    }

    void BeginPlayback() {
        playback_in_flight_++;
    }

    void FinishPlayback() {
        if (playback_in_flight_ > 0) {
            playback_in_flight_--;
        }
    }

    void CancelQueuedPlayback() {
        generation_++;
    }

    bool IsCurrent(uint32_t generation) const {
        return generation == generation_;
    }

    bool HasInFlight() const {
        return decode_in_flight_ > 0 || playback_in_flight_ > 0;
    }

    bool IsPlaybackDrained(bool decode_queue_empty, bool playback_queue_empty) const {
        return decode_queue_empty && playback_queue_empty &&
            decode_in_flight_ == 0 && playback_in_flight_ == 0;
    }

    bool IsIdle(bool encode_queue_empty, bool decode_queue_empty,
                bool playback_queue_empty, bool testing_queue_empty) const {
        return encode_queue_empty && testing_queue_empty &&
            IsPlaybackDrained(decode_queue_empty, playback_queue_empty);
    }

    uint32_t decode_in_flight() const { return decode_in_flight_; }
    uint32_t playback_in_flight() const { return playback_in_flight_; }
    uint32_t generation() const { return generation_; }

private:
    uint32_t decode_in_flight_ = 0;
    uint32_t playback_in_flight_ = 0;
    uint32_t generation_ = 0;
};

#endif  // AUDIO_PLAYBACK_DRAIN_TRACKER_H
