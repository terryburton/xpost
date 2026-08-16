/*
 * Xpost - a Level-2 Postscript interpreter
 * Copyright (C) 2013-2016, Michael Joshua Ryan
 * Copyright (C) 2013, Vincent Torri
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the Xpost software product nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef XPOST_MAIN_H
#define XPOST_MAIN_H

/**
 * @file xpost_main.h
 * @brief Initializing and quitting functions
 */

/**
 * @brief Ask to be called when the library goes down.
 *
 * The library's lifetime is counted: the quit that balances the last
 * init takes it down, and a process may then start another lifetime. A
 * module holding something for that lifetime -- a cache, an open handle,
 * a table it allocated -- has to give it back at that point, or the next
 * lifetime is handed what the last one freed.
 *
 * A module says so by registering here, at the moment it takes the thing
 * it will have to give back. Acquiring and releasing are then written
 * together, and there is no separate list for a new module to be left
 * out of. Registering the same function again is not an error and does
 * not register it twice, so the call may sit on the acquisition path and
 * run as often as that path does.
 *
 * The registered functions run in the reverse of the order they
 * registered in, so a module reaches what it was built on before that
 * goes; the list is emptied as they run, and a later lifetime registers
 * afresh. Coming up later means going down sooner, so a module is torn
 * down before whatever it was built on: the findfont cache gives its
 * faces back while the font library that owns them is still there to
 * take them, because it registered after that library did.
 *
 * This is the whole of the teardown -- xpost_quit calls what is
 * registered and nothing else, so a module is taken down because it
 * asked to be and not because someone remembered it. An init that gives
 * up partway runs the same list, so what had come up before the refusal
 * does not stay up.
 *
 * A module registers itself, on the path where it takes what it will
 * give back. Two cannot: compat and the log are shared with
 * libxpost_dsc, which has no lifetime of its own to register against, so
 * xpost_init registers those two where it brings them up.
 *
 * @param fn The function to call. Ignored when NULL.
 * @return 1 when it is registered or was already, 0 when the table is
 *         full -- which is also reported on the error log.
 */
int xpost_at_quit(void (*fn)(void));

#endif
