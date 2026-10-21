// SPDX-FileCopyrightText: Copyright 2026 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Event-thread glue between the boundary inventory recorder (core) and its manual controls (N0):
// the hotkeys and the overlay window call Apply, which ends segments and writes their files.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#include "common/env_option.h"
#include "common/fs/fs_util.h"
#include "common/logging.h"
#include "core/boundary_inventory.h"
#include "core/boundary_inventory_report.h"
#include "suyu_cmd/overlay/inventory_session.h"
#include "suyu_cmd/overlay/overlay_actions.h"

namespace SuyuCmd {

class InventoryHost {
public:
    /// Starts paused unless SUYU_BOUNDARY_INVENTORY_START=1, so loading screens stay out.
    InventoryHost() : session{StartsRecording(), InventoryNowNs()} {
        Core::BoundaryInventory::Instance().SetRecording(session.Recording());
    }

    static bool Wanted() {
        return Core::BoundaryInventory::Enabled();
    }

    UiActionResult Apply(const InventoryRequest& request) {
        using Kind = InventoryRequest::Kind;
        switch (request.kind) {
        case Kind::Mark:
            return Mark();
        case Kind::TogglePause:
            return TogglePause();
        case Kind::SkipStep:
            return SkipStep();
        case Kind::WriteNow:
            return WriteNow();
        case Kind::SetLabel:
            return SetLabel(request.label);
        }
        return Remember({false, "Inventory: unknown request"});
    }

    InventorySnapshot Snapshot() const {
        return session.Snapshot(InventoryNowNs(), last_ok, last_message);
    }

    /// Last chance to write ended segments before the window goes away; reports what is lost.
    void Finalize() {
        const PendingSegmentFiles::Outcome outcome = pending.Flush(WriteSegmentFile);
        if (!outcome.ok || !outcome.lost.empty()) {
            LOG_ERROR(Frontend, "Boundary inventory: {} ended segment file(s) were never written: {}",
                      pending.Size(), outcome.error);
        }
    }

private:

    static bool StartsRecording() {
        const char* value = std::getenv("SUYU_BOUNDARY_INVENTORY_START");
        return value && Common::EnvFlagDefaultOff(value);
    }

    UiActionResult Remember(UiActionResult result) {
        last_ok = result.ok;
        last_message = result.message;
        return result;
    }

    UiActionResult Mark() {
        const InventorySession::EndedSegment ended = session.Mark(InventoryNowNs());
        pending.Add(Core::BoundaryInventory::SegmentFileName(Core::BoundaryInventory::RunLabel(),
                                                             ended.serial, ended.label),
                    Core::BoundaryInventory::Instance().EndSegment(ended.label, ended.elapsed_ns));
        return FlushUnwritten("segment '" + ended.label + "' ended");
    }

    UiActionResult TogglePause() {
        const bool recording = !session.Recording();
        session.SetRecording(recording, InventoryNowNs());
        Core::BoundaryInventory::Instance().SetRecording(recording);
        return Remember({true, recording ? "Inventory: recording" : "Inventory: paused"});
    }

    UiActionResult SkipStep() {
        session.SkipStep();
        return Remember({true, "Inventory: next segment is '" + session.Label() + "'"});
    }

    UiActionResult SetLabel(const std::string& label) {
        session.SetCustomLabel(label.substr(0, kInventoryLabelCapacity));
        return Remember({true, "Inventory: this segment is '" + session.Label() + "'"});
    }

    UiActionResult WriteNow() {
        std::string error;
        if (!Core::BoundaryInventory::WriteReport(&error)) {
            return Remember({false, "Inventory: " + error});
        }
        return FlushUnwritten("open segment saved");
    }

    static bool WriteSegmentFile(const std::string& file_name, const std::string& text,
                                 std::string& error) {
        return Core::BoundaryInventory::WriteTextFile(
            Core::BoundaryInventory::OutputDir() / file_name, text, error);
    }

    /// Writes the waiting segment files in order; the first failure stops and keeps the rest.
    UiActionResult FlushUnwritten(const std::string& what) {
        const PendingSegmentFiles::Outcome outcome = pending.Flush(WriteSegmentFile);
        std::string lost;
        for (const std::string& file_name : outcome.lost) {
            lost += (lost.empty() ? "" : ", ") + file_name;
        }
        if (!lost.empty()) {
            LOG_ERROR(Frontend, "Boundary inventory: segment file(s) lost, the queue was full: {}",
                      lost);
        }
        if (!outcome.ok) {
            return Remember({false, "Inventory: " + outcome.error + " (" +
                                        std::to_string(pending.Size()) + " file(s) kept for a retry)" +
                                        (lost.empty() ? std::string{} : "; LOST: " + lost)});
        }
        return Remember({true, "Inventory: " + what +
                                   (outcome.last_file.empty() ? std::string{}
                                                              : " -> " + outcome.last_file)});
    }

    InventorySession session;
    PendingSegmentFiles pending;
    bool last_ok{true};
    std::string last_message;
};

} // namespace SuyuCmd
