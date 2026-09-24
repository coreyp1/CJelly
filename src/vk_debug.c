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
 * @file vk_debug.c
 *
 * The bridge between the Vulkan validation layer and the log.
 */

#include <ghoti.io/cjelly/macros.h>
#include <ghoti.io/cjelly/cj_log.h>
#include <ghoti.io/cjelly/log_internal.h>
#include <ghoti.io/cjelly/vk_debug_internal.h>

/**
 * @brief Hands a validation layer message to the log at its own severity.
 *
 * The severity the layer assigns is the only thing that can decide how loud
 * one of these is; printing them all at one volume - which this did, at full
 * volume, whenever the validation layers were on at all - is what made the
 * layers something to turn off rather than something to read.
 *
 * The mapping itself is cj_log__level_for_vk_severity, which is separate so
 * that it can be tested without provoking a real validation failure.
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL cj_vk_debug__callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    CJ_MAYBE_UNUSED(VkDebugUtilsMessageTypeFlagsEXT messageTypes),
    const VkDebugUtilsMessengerCallbackDataEXT * pCallbackData,
    CJ_MAYBE_UNUSED(void * pUserData)) {
  /* %s on a message the layer owns: it is not a format string, and treating
   * it as one would let a layer's text steer printf. */
  CJ_LOG_AT(cj_log__level_for_vk_severity(messageSeverity),
      "validation: %s", pCallbackData->pMessage);
  return VK_FALSE;
}

cj_log_level_t cj_log__level_for_vk_severity(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
  /* Most severe bit first: a message carrying both ERROR and WARNING is an
   * error, and testing WARNING first would quietly demote it. */
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    return CJ_LOG_ERROR;
  }
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    return CJ_LOG_WARN;
  }
  /* VERBOSE and INFO are both the layer narrating its own bookkeeping, and
   * they differ by less than the gap between DEBUG and TRACE here. A
   * severity of 0, which is not a thing a layer should send, lands here too
   * rather than being treated as an error. */
  return CJ_LOG_DEBUG;
}

void cj_vk_debug__describe(VkDebugUtilsMessengerCreateInfoEXT * out_info) {
  if (!out_info) {
    return;
  }
  VkDebugUtilsMessengerCreateInfoEXT info = {0};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  /* WARNING and ERROR only. INFO and VERBOSE from the layer are a running
   * commentary on every object created, which at TRACE would bury the
   * library's own trace output in someone else's. */
  info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  info.pfnUserCallback = cj_vk_debug__callback;
  *out_info = info;
}

VkResult cj_vk_debug__create(
    VkInstance instance, VkDebugUtilsMessengerEXT * out_messenger) {
  if (!out_messenger) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }
  *out_messenger = VK_NULL_HANDLE;
  if (instance == VK_NULL_HANDLE) {
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  /* An extension entry point: not in the loader's static exports, so it is
   * resolved by name. A NULL here is the instance having been created
   * without VK_EXT_debug_utils. */
  PFN_vkCreateDebugUtilsMessengerEXT create =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkCreateDebugUtilsMessengerEXT");
  if (!create) {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }

  VkDebugUtilsMessengerCreateInfoEXT info;
  cj_vk_debug__describe(&info);
  return create(instance, &info, NULL, out_messenger);
}

void cj_vk_debug__destroy(
    VkInstance instance, VkDebugUtilsMessengerEXT messenger) {
  if (instance == VK_NULL_HANDLE || messenger == VK_NULL_HANDLE) {
    return;
  }
  PFN_vkDestroyDebugUtilsMessengerEXT destroy =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkDestroyDebugUtilsMessengerEXT");
  if (destroy) {
    destroy(instance, messenger, NULL);
  }
}
