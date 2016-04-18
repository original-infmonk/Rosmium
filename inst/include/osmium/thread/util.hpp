#ifndef OSMIUM_THREAD_UTIL_HPP
#define OSMIUM_THREAD_UTIL_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2015 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <chrono>
#include <future>

#ifdef __linux__
# include <sys/prctl.h>
#endif

namespace osmium {

    namespace thread {

        /**
         * Check if the future resulted in an exception. This will re-throw
         * the exception stored in the future if there was one. Otherwise it
         * will just return.
         */
        template <typename T>
        inline void check_for_exception(std::future<T>& future) {
            if (future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                future.get();
            }
        }

        /**
         * Wait until the given future becomes ready. Will block if the future
         * is not ready. Can be called more than once unlike future.get().
         */
        template <typename T>
        inline void wait_until_done(std::future<T>& future) {
            if (future.valid()) {
                future.get();
            }
        }

        /**
         * Set name of current thread for debugging. This only works on Linux.
         */
#ifdef __linux__
        inline void set_thread_name(const char* name) noexcept {
            prctl(PR_SET_NAME, name, 0, 0, 0);
        }
#else
        inline void set_thread_name(const char*) noexcept {
            // intentionally left blank
        }
#endif

        class thread_handler {

            std::thread m_thread;

        public:

            thread_handler() :
                m_thread() {
            }

            template <typename TFunction, typename... TArgs>
            explicit thread_handler(TFunction&& f, TArgs&&... args) :
                m_thread(std::forward<TFunction>(f), std::forward<TArgs>(args)...) {
            }

            thread_handler(const thread_handler&) = delete;
            thread_handler& operator=(const thread_handler&) = delete;

            thread_handler(thread_handler&&) = default;
            thread_handler& operator=(thread_handler&&) = default;

            ~thread_handler() {
                if (m_thread.joinable()) {
                    m_thread.join();
                }
            }

        }; // class thread_handler

    } // namespace thread

} // namespace osmium

#endif //  OSMIUM_THREAD_UTIL_HPP