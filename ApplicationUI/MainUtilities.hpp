#pragma once

#include <QGuiApplication>
#include <Qt>

/// @brief RAII guard that sets the Qt busy cursor for the lifetime of the scope.
///
/// Usage:
/// {
///     BusyCursorGuard busy;
///     // long-running UI-thread operation kickoff
/// }
///
/// Guarantees the cursor is restored on scope exit,
/// even with early returns or exceptions.
///
/// IMPORTANT:
/// - Must be created and destroyed on the UI thread.
/// - Does NOT protect against process crashes.
struct BusyCursorGuard
{
    BusyCursorGuard()
    {
        QGuiApplication::setOverrideCursor(Qt::BusyCursor);
    }

    ~BusyCursorGuard()
    {
        // Defensive: only restore if one is set
        if (QGuiApplication::overrideCursor())
            QGuiApplication::restoreOverrideCursor();
    }

    BusyCursorGuard(const BusyCursorGuard&)            = delete;
    BusyCursorGuard& operator=(const BusyCursorGuard&) = delete;
};
