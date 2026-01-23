#pragma once

#include <iostream>

#include "SceneFormat.hpp"

inline void dumpSceneIOReport(const SceneIOReport& report)
{
    for (const SceneIOMessage& m : report.messages)
    {
        switch (m.type)
        {
            case SceneIOMessage::Type::Info:
                std::cerr << "[SceneIO][Info] ";
                break;
            case SceneIOMessage::Type::Warning:
                std::cerr << "[SceneIO][Warning] ";
                break;
            case SceneIOMessage::Type::Error:
                std::cerr << "[SceneIO][Error] ";
                break;
        }

        std::cerr << m.text << '\n';
    }

    if (report.status != SceneIOStatus::Ok)
    {
        std::cerr << "[SceneIO] Final status = "
                  << static_cast<int>(report.status) << '\n';
    }
}
