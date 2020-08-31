/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *  Copyright (C) 2013-2014 - Jason Fetters
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <unistd.h>

#include <retro_miscellaneous.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#include "../input_keymaps.h"

#include "cocoa_input.h"

#include "../../retroarch.h"
#include "../../driver.h"

#include "../drivers_keyboard/keyboard_event_apple.h"

/* TODO/FIXME -
 * fix game focus toggle */

/* Forward declarations */
float get_backing_scale_factor(void);

static void *cocoa_input_init(const char *joypad_driver)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)calloc(1, sizeof(*apple));
   if (!apple)
      return NULL;

   input_keymaps_init_keyboard_lut(rarch_key_map_apple_hid);

   return apple;
}

static void cocoa_input_poll(void *data)
{
   uint32_t i;
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;
#ifndef IOS
   float   backing_scale_factor = get_backing_scale_factor();
#endif

   if (!apple)
      return;

   for (i = 0; i < apple->touch_count; i++)
   {
      struct video_viewport vp;

      vp.x                        = 0;
      vp.y                        = 0;
      vp.width                    = 0;
      vp.height                   = 0;
      vp.full_width               = 0;
      vp.full_height              = 0;

#ifndef IOS
      apple->touches[i].screen_x *= backing_scale_factor;
      apple->touches[i].screen_y *= backing_scale_factor;
#endif
      video_driver_translate_coord_viewport_wrap(
            &vp,
            apple->touches[i].screen_x,
            apple->touches[i].screen_y,
            &apple->touches[i].fixed_x,
            &apple->touches[i].fixed_y,
            &apple->touches[i].full_x,
            &apple->touches[i].full_y);
   }
}

static int16_t cocoa_input_state(
      void *data,
      const input_device_driver_t *joypad,
      const input_device_driver_t *sec_joypad,
      rarch_joypad_info_t *joypad_info,
      const struct retro_keybind **binds,
      bool keyboard_mapping_blocked,
      unsigned port,
      unsigned device,
      unsigned idx,
      unsigned id)
{
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
         {
            unsigned i;
            /* Do a bitwise OR to combine both input
             * states together */
            int16_t ret = joypad->state(
                  joypad_info, binds[port], port)
#ifdef HAVE_MFI
                 | sec_joypad->state(
                     joypad_info, binds[port], port)
#endif
                 ;

            if (!keyboard_mapping_blocked)
            {
               for (i = 0; i < RARCH_FIRST_CUSTOM_BIND; i++)
               {
                  if (
                        (binds[port][i].key < RETROK_LAST) &&
                        apple_key_state[rarch_keysym_lut[binds[port][i].key]] & 0x80)
                     ret |= (1 << i);
               }
            }
            return ret;
         }
         else
         {
            if (binds[port][id].valid)
            {
               if (button_is_pressed(
                        joypad,
                        joypad_info, binds[port], port, id))
                  return 1;
#ifdef HAVE_MFI
               else if (button_is_pressed(
                        sec_joypad,
                        joypad_info, binds[port], port, id))
                  return 1;
#endif
               else if (id < RARCH_BIND_LIST_END)
                  if (apple_key_state[rarch_keysym_lut[binds[port][id].key]])
                     return 1;
            }
         }
         break;
      case RETRO_DEVICE_ANALOG:
         break;
      case RETRO_DEVICE_KEYBOARD:
         return (id < RETROK_LAST) && apple_key_state[rarch_keysym_lut[(enum retro_key)id]];
      case RETRO_DEVICE_MOUSE:
      case RARCH_DEVICE_MOUSE_SCREEN:
         {
            int16_t val = 0;
            switch (id)
            {
               case RETRO_DEVICE_ID_MOUSE_X:
                  if (device == RARCH_DEVICE_MOUSE_SCREEN)
                  {
#ifdef IOS
                     return apple->window_pos_x;
#else
                     return apple->window_pos_x * get_backing_scale_factor();
#endif
                  }
                  val = apple->window_pos_x - apple->mouse_x_last;
                  apple->mouse_x_last = apple->window_pos_x;
                  return val;
               case RETRO_DEVICE_ID_MOUSE_Y:
                  if (device == RARCH_DEVICE_MOUSE_SCREEN)
                  {
#ifdef IOS
                     return apple->window_pos_y;
#else
                     return apple->window_pos_y * get_backing_scale_factor();
#endif
                  }
                  val = apple->window_pos_y - apple->mouse_y_last;
                  apple->mouse_y_last = apple->window_pos_y;
                  return val;
               case RETRO_DEVICE_ID_MOUSE_LEFT:
                  return apple->mouse_buttons & 1;
               case RETRO_DEVICE_ID_MOUSE_RIGHT:
                  return apple->mouse_buttons & 2;
               case RETRO_DEVICE_ID_MOUSE_WHEELUP:
                  return apple->mouse_wu;
               case RETRO_DEVICE_ID_MOUSE_WHEELDOWN:
                  return apple->mouse_wd;
               case RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP:
                  return apple->mouse_wl;
               case RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN:
                  return apple->mouse_wr;
            }
         }
         break;
      case RETRO_DEVICE_POINTER:
      case RARCH_DEVICE_POINTER_SCREEN:
         {
            const bool want_full = (device == RARCH_DEVICE_POINTER_SCREEN);

            if (idx < apple->touch_count && (idx < MAX_TOUCHES))
            {
               int16_t x, y;
               const cocoa_touch_data_t *touch = (const cocoa_touch_data_t *)
                  &apple->touches[idx];

               if (!touch)
                  return 0;

               x = touch->fixed_x;
               y = touch->fixed_y;

               if (want_full)
               {
                  x = touch->full_x;
                  y = touch->full_y;
               }

               switch (id)
               {
                  case RETRO_DEVICE_ID_POINTER_PRESSED:
                     return (x != -0x8000) && (y != -0x8000);
                  case RETRO_DEVICE_ID_POINTER_X:
                     return x;
                  case RETRO_DEVICE_ID_POINTER_Y:
                     return y;
                  case RETRO_DEVICE_ID_POINTER_COUNT:
                     return apple->touch_count;
               }
            }
         }
         break;
   }

   return 0;
}

static void cocoa_input_free(void *data)
{
   unsigned i;
   cocoa_input_data_t *apple = (cocoa_input_data_t*)data;

   if (!apple || !data)
      return;

   for (i = 0; i < MAX_KEYS; i++)
      apple_key_state[i] = 0;

   free(apple);
}

static bool cocoa_input_set_rumble(
      const input_device_driver_t *joypad,
      const input_device_driver_t *sec_joypad,
      unsigned port, enum retro_rumble_effect effect, uint16_t strength)
{
   if (joypad)
      return input_joypad_set_rumble(joypad,
            port, effect, strength);
#ifdef HAVE_MFI
    if (sec_joypad)
        return input_joypad_set_rumble(sec_joypad,
            port, effect, strength);
#endif
   return false;
}

static uint64_t cocoa_input_get_capabilities(void *data)
{
   return
      (1 << RETRO_DEVICE_JOYPAD)   |
      (1 << RETRO_DEVICE_MOUSE)    |
      (1 << RETRO_DEVICE_KEYBOARD) |
      (1 << RETRO_DEVICE_POINTER)  |
      (1 << RETRO_DEVICE_ANALOG);
}

input_driver_t input_cocoa = {
   cocoa_input_init,
   cocoa_input_poll,
   cocoa_input_state,
   cocoa_input_free,
   NULL,
   NULL,
   cocoa_input_get_capabilities,
   "cocoa",
   NULL,                         /* grab_mouse */
   NULL,
   cocoa_input_set_rumble
};
