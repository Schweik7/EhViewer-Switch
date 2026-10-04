#include "App.h"
#include "AppShared.h"

#include <algorithm>
#include <cmath>

#ifdef __SWITCH__
#include <SDL2/SDL.h>
#endif

using namespace app_shared;

ehviewer::ListGeometry App::Geometry() const
{
    return ehviewer::GalleryListGeometry(m_settings.list_layout);
}

double App::ListMaxScroll() const
{
    // Leave room for the "loading more / end of list" footer.
    return Geometry().MaxScroll(m_galleries.size()) + (m_galleries.empty() ? 0.0 : 48.0);
}

void App::ScrollListToSelection()
{
    m_list_velocity = 0.0;
    m_list_scroll_target = std::min(Geometry().ScrollToShow(m_selected, m_list_scroll_target, m_galleries.size()),
                                    ListMaxScroll());
}

bool App::AnimateListScroll()
{
    const double before = m_list_scroll;
    const double max_scroll = ListMaxScroll();
    if (!m_touch_down && std::fabs(m_list_velocity) > 0.4)
    {
        // Fling after a touch drag.
        m_list_scroll += m_list_velocity;
        m_list_velocity *= 0.94;
        m_list_scroll_target = m_list_scroll;
    }
    else if (!m_touch_down)
    {
        m_list_velocity = 0.0;
        const double distance = m_list_scroll_target - m_list_scroll;
        m_list_scroll = std::fabs(distance) < 0.5 ? m_list_scroll_target : m_list_scroll + distance * 0.32;
    }
    m_list_scroll = std::max(0.0, std::min(m_list_scroll, max_scroll));
    m_list_scroll_target = std::max(0.0, std::min(m_list_scroll_target, max_scroll));
    return std::fabs(m_list_scroll - before) > 0.01;
}

void App::MaybeLoadMore()
{
    if (m_galleries.empty() || m_list_nav.next_url.empty() || m_task_type != TaskType::None ||
        m_list_source == ListSource::History || SDL_GetTicks() < m_load_more_retry_tick)
        return;
    const ehviewer::ListGeometry geometry = Geometry();
    const bool near_end_by_selection =
        m_selected + static_cast<std::size_t>(geometry.columns) * 2 >= m_galleries.size();
    const bool near_end_by_scroll = m_list_scroll + geometry.viewport_height >=
                                    geometry.ContentHeight(m_galleries.size()) - 400;
    if (near_end_by_selection || near_end_by_scroll)
        LoadGalleryList(m_list_nav.next_url, m_list_page_number + 1, true);
}
