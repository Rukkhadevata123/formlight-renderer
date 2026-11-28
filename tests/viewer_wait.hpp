#pragma once

import std;
import formlight.preview;
import formlight.viewer.session;

namespace formlight::test
{
// Keep the simulated navigation clock fixed while waiting for the background worker.
inline const ColorImage &await_frame(ViewerSession &session, ViewerSession::Clock::time_point now)
{
    while (true)
    {
        if (const auto *image = session.update({}, now))
        {
            return *image;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
}

inline const PixelDiagnostic &await_report(ViewerSession &session, ViewerSession::Clock::time_point now)
{
    while (!session.report())
    {
        (void)session.update({}, now);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return *session.report();
}
} // namespace formlight::test
