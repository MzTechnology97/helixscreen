// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>

namespace helix::ui {

/**
 * @brief K2-OpenHost slot editor card: the print parameters Kalico keeps for
 * the slot (profile, pressure advance, max flow, nozzle temperature) and its
 * runout group, below the stock spool card.
 *
 * Pressure advance steps in 0.002 and is saved with Apply
 * (_BOX_SLOT_PA_SET); the rest is read-only. The card owns its state and
 * frees it when the editor's tree is deleted.
 *
 * @param form_view The editor's form view (the card goes at its end)
 * @param slot_index Edited slot, 0-based; -2 for the external spool
 */
void k2oh_add_slot_params(lv_obj_t* form_view, int slot_index);

} // namespace helix::ui
