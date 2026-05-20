/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <unistd.h>

#include <utility>

namespace igl::android {

/**
 * RAII wrapper around a POSIX file descriptor.
 *
 * Owns the descriptor and closes it on destruction. Move-only; copying is
 * disabled to enforce single ownership.
 *
 * Conventions:
 *   - A negative descriptor (default -1) means "no fd owned".
 *   - get() returns the raw fd without transferring ownership.
 *   - release() returns the raw fd and gives up ownership; caller must close.
 *   - reset() closes the currently owned fd (if any) and optionally takes a
 *     new one.
 */
class UniqueFd {
 public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) noexcept : fd_(fd) {}

  ~UniqueFd() {
    reset();
  }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  UniqueFd(UniqueFd&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
  }

  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept {
    return fd_;
  }

  [[nodiscard]] bool valid() const noexcept {
    return fd_ >= 0;
  }

  explicit operator bool() const noexcept {
    return valid();
  }

  /// Relinquishes ownership of the descriptor and returns it. The caller is
  /// responsible for closing it.
  [[nodiscard]] int release() noexcept {
    int f = fd_;
    fd_ = -1;
    return f;
  }

  /// Closes the currently owned fd (if any) and optionally adopts a new one.
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0 && fd_ != fd) {
      ::close(fd_);
    }
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

} // namespace igl::android
