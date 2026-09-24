/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2025-2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io CJelly.
 *
 * Ghoti.io CJelly is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io CJelly is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file vk_debug_internal.h
 *
 * Where a Vulkan validation message is turned into a log line.
 *
 * Enabling the validation layer is only half of what it takes to hear from
 * it. The layer reports through a debug messenger the application registers;
 * an instance that switches the layer on and registers nothing runs every
 * check and discards every answer, and looks exactly like an instance with
 * nothing wrong. Two places in this library create instances, and one of them
 * had only the first half - so "the demo reports no validation errors" was
 * true of a demo that could not report one.
 *
 * These four calls are that second half, in one place, so the next instance
 * to be created cannot get only part of it.
 */

#ifndef GHOTI_IO_CJ_VK_DEBUG_INTERNAL_H
#define GHOTI_IO_CJ_VK_DEBUG_INTERNAL_H

#include <ghoti.io/cjelly/macros.h>

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The instance extension a messenger needs.
 *
 * Requesting the layer without this extension is accepted by the loader and
 * then vkCreateDebugUtilsMessengerEXT cannot be resolved, which is the quiet
 * half of the failure this header exists to prevent.
 */
#define CJ_VK_DEBUG_EXTENSION_NAME VK_EXT_DEBUG_UTILS_EXTENSION_NAME

/** Fill in the messenger description this library uses.
 *
 * One description for every messenger in the library: the severities
 * subscribed to here are the only ones that can ever reach the log, so having
 * two copies of them would mean one instance quietly hearing less than the
 * other.
 *
 * @param out_info Zeroed and filled in. Nothing is allocated, so the caller
 *                 may keep it on the stack - including as the pNext of a
 *                 VkInstanceCreateInfo, which is what catches the messages
 *                 emitted during instance creation itself.
 */
void cj_vk_debug__describe(VkDebugUtilsMessengerCreateInfoEXT * out_info);

/** Register a messenger against an instance.
 *
 * @param instance The instance, which must have been created with
 *                 CJ_VK_DEBUG_EXTENSION_NAME enabled.
 * @param out_messenger Receives the messenger. Set to VK_NULL_HANDLE on
 *                 failure, so the caller can pass it to the destroy call
 *                 unconditionally.
 * @return VK_SUCCESS, or the failure - including
 *         VK_ERROR_EXTENSION_NOT_PRESENT when the entry point cannot be
 *         resolved, which is what a missing extension looks like from here.
 */
VkResult cj_vk_debug__create(
    VkInstance instance, VkDebugUtilsMessengerEXT * out_messenger);

/** Unregister a messenger. VK_NULL_HANDLE is accepted and does nothing.
 *
 * Must happen before vkDestroyInstance: the messenger belongs to the
 * instance, and leaving it registered is itself a validation error - one that
 * by definition no longer has anywhere to be reported.
 */
void cj_vk_debug__destroy(
    VkInstance instance, VkDebugUtilsMessengerEXT messenger);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // GHOTI_IO_CJ_VK_DEBUG_INTERNAL_H
