// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_GPU_MSD_ARM_MALI_SRC_JOB_SCHEDULER_H_
#define GARNET_DRIVERS_GPU_MSD_ARM_MALI_SRC_JOB_SCHEDULER_H_

#include <chrono>
#include <functional>
#include <list>
#include <vector>

#include "magma_util/macros.h"
#include "msd_arm_atom.h"

class JobScheduler {
 public:
  class Owner {
   public:
    virtual void RunAtom(MsdArmAtom* atom) = 0;
    virtual void AtomCompleted(MsdArmAtom* atom, ArmMaliResultCode result_code) = 0;
    virtual void HardStopAtom(MsdArmAtom* atom) {}
    virtual void SoftStopAtom(MsdArmAtom* atom) {}
    virtual void ReleaseMappingsForAtom(MsdArmAtom* atom) {}
    virtual magma::PlatformPort* GetPlatformPort() { return nullptr; }
    virtual void UpdateGpuActive(bool active) {}
    virtual bool IsInProtectedMode() = 0;
    virtual void EnterProtectedMode() = 0;
    virtual bool ExitProtectedMode() = 0;
    virtual void OutputHangMessage() = 0;
  };
  using Clock = std::chrono::steady_clock;

  JobScheduler(Owner* owner, uint32_t job_slots);

  void EnqueueAtom(std::shared_ptr<MsdArmAtom> atom);
  void TryToSchedule();
  void PlatformPortSignaled(uint64_t key);

  void CancelAtomsForConnection(std::shared_ptr<MsdArmConnection> connection);

  void JobCompleted(uint64_t slot, ArmMaliResultCode result_code, uint64_t tail);

  uint32_t job_slots() const { return job_slots_; }

  size_t GetAtomListSize();

  // Gets the duration until the earliest currently executing or waiting atom should time out, or
  // max if there's no timeout pending.
  Clock::duration GetCurrentTimeoutDuration();

  void HandleTimedOutAtoms();

  void ReleaseMappingsForConnection(std::shared_ptr<MsdArmConnection> connection);

 private:
  MsdArmAtom* executing_atom() const { return executing_atoms_[0].get(); }
  void ProcessSoftAtom(std::shared_ptr<MsdArmSoftAtom> atom);
  void SoftJobCompleted(std::shared_ptr<MsdArmSoftAtom> atom);
  void UpdatePowerManager();
  void MoveAtomsToRunnable();
  void ScheduleRunnableAtoms();
  uint32_t num_executing_atoms() {
    uint32_t count = 0;
    for (auto& atom : executing_atoms_) {
      if (atom) {
        count++;
      }
    }
    return count;
  }
  void ValidateCanSwitchProtected();

  void set_timeout_duration(uint64_t timeout_duration_ms) {
    timeout_duration_ms_ = timeout_duration_ms;
  }
  void set_semaphore_timeout_duration(uint64_t semaphore_timeout_duration_ms) {
    semaphore_timeout_duration_ms_ = semaphore_timeout_duration_ms;
  }

  Owner* owner_;

  uint32_t job_slots_;

  // This duration determines how often to check whether this atom should be preempted by another
  // of the same priority.
  uint64_t job_tick_duration_ms_ = 100;

  uint64_t timeout_duration_ms_ = 2000;
  // Semaphore timeout is longer because one semaphore may need to wait for a
  // lot of atoms to complete.
  uint64_t semaphore_timeout_duration_ms_ = 5000;

  // If we want to switch to a mode, then hold off submitting atoms in the
  // other mode until that switch is complete.
  bool want_to_switch_to_protected_ = false;
  bool want_to_switch_to_nonprotected_ = false;

  std::vector<std::shared_ptr<MsdArmSoftAtom>> waiting_atoms_;
  std::vector<std::shared_ptr<MsdArmAtom>> executing_atoms_;
  std::list<std::shared_ptr<MsdArmAtom>> atoms_;
  std::vector<std::list<std::shared_ptr<MsdArmAtom>>> runnable_atoms_;

  friend class TestJobScheduler;

  DISALLOW_COPY_AND_ASSIGN(JobScheduler);
};

#endif  // GARNET_DRIVERS_GPU_MSD_ARM_MALI_SRC_JOB_SCHEDULER_H_
