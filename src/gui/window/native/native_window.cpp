// native_window.cpp
/*
  neogfx C++ GUI Library
  Copyright (c) 2015-present, Leigh Johnston.  All Rights Reserved.
  
  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <neogfx/neogfx.hpp>
#include <iterator>
#ifdef _WIN32
#include <D2d1.h>
#endif

#include <neolib/raii.hpp>
#include <neogfx/app/app.hpp>
#include <neogfx/gfx/i_rendering_engine.hpp>
#include <neogfx/hid/i_surface_manager.hpp>
#include <neogfx/hid/i_surface_window.hpp>
#include "native_window.hpp"

namespace neogfx
{
	native_window::native_window(i_rendering_engine& aRenderingEngine, i_surface_manager& aSurfaceManager) :
		iRenderingEngine{ aRenderingEngine },
		iSurfaceManager{ aSurfaceManager },
		iProcessingEvent{ 0u },
		iNonClientEntered{ false },
		iUpdater{ app::instance(), [this](neolib::callback_timer& aTimer)
		{
			aTimer.again();
			if (non_client_entered() && 
				surface_window().native_window_hit_test(surface_window().as_window().window_manager().mouse_position(surface_window().as_window())) == widget_part::Nowhere)
			{
				auto e1 = find_event<window_event>(window_event_type::NonClientLeave);
				auto e2 = find_event<window_event>(window_event_type::NonClientEnter);
				if (e1 == iEventQueue.end() || (e2 != iEventQueue.end() && 
					std::distance(iEventQueue.cbegin(), e1) < std::distance(iEventQueue.cbegin(), e2)))
					push_event(window_event{ window_event_type::NonClientLeave });
			}
		}, 10 }
	{
	}

	native_window::~native_window()
	{
	}

	dimension native_window::horizontal_dpi() const
	{
		return pixel_density().cx;
	}

	dimension native_window::vertical_dpi() const
	{
		return pixel_density().cy;
	}

	dimension native_window::ppi() const
	{
		return pixel_density().magnitude() / std::sqrt(2.0);
	}

	dimension native_window::em_size() const
	{
		return 0;
	}

	void native_window::display_error_message(const std::string& aTitle, const std::string& aMessage) const
	{
		iSurfaceManager.display_error_message(*this, aTitle, aMessage);
	}

	bool native_window::events_queued() const
	{
		return !iEventQueue.empty();
	}

	void native_window::push_event(const native_event& aEvent)
	{
		if (aEvent.is<window_event>())
		{
			const auto& windowEvent = static_variant_cast<const window_event&>(aEvent);
			switch (windowEvent.type())
			{
			case window_event_type::Resized:
			case window_event_type::SizeChanged:
				for (auto e = iEventQueue.begin(); e != iEventQueue.end();)
				{
					if (e->is<window_event>() && static_variant_cast<const window_event&>(*e).type() == windowEvent.type())
						e = iEventQueue.erase(e);
					else
						++e;
				}
				break;
			default:
				break;
			}
		}
		iEventQueue.push_back(aEvent);
	}

	bool native_window::pump_event()
	{
		neolib::scoped_counter sc{ iProcessingEvent };
		if (iEventQueue.empty())
			return false;
		auto e = iEventQueue.front();
		iEventQueue.pop_front();
		handle_event(e);
		return true;
	}

	void native_window::handle_event(const native_event& aEvent)
	{
		neolib::destroyed_flag destroyed{ *this };
		neolib::scoped_counter sc{ iProcessingEvent };
		iCurrentEvent = aEvent;
		handle_event();
		if (!destroyed)
			iCurrentEvent = boost::none;
		else
			sc.ignore();
	}

	bool native_window::has_current_event() const
	{
		return iCurrentEvent != boost::none;
	}

	const native_window::native_event& native_window::current_event() const
	{
		if (iCurrentEvent != boost::none)
			return iCurrentEvent;
		throw no_current_event();
	}

	void native_window::handle_event()
	{
		neolib::destroyed_flag destroyed{ *this };
		neolib::scoped_counter sc{ iProcessingEvent };
		if (!filter_event.trigger(iCurrentEvent))
		{
			if (destroyed)
				sc.ignore();
			return;
		}
		if (iCurrentEvent.is<window_event>())
		{
			auto& windowEvent = static_variant_cast<window_event&>(iCurrentEvent);
			if (!surface_window().as_window().window_event.trigger(windowEvent))
				return;
			switch (windowEvent.type())
			{
			case window_event_type::Paint:
				invalidate(surface_size());
				render(true);
				break;
			case window_event_type::Close:
				close();
				break;
			case window_event_type::Resizing:
				surface_window().native_window_resized();
				for (auto e = iEventQueue.begin(); e != iEventQueue.end();)
				{
					if (e->is<window_event>())
					{
						switch (static_variant_cast<const window_event&>(*e).type())
						{
						case window_event_type::Resized:
						case window_event_type::SizeChanged:
							e = iEventQueue.erase(e);
							break;
						default:
							++e;
							break;
						}
					}
				}
				break;
			case window_event_type::Resized:
				surface_window().native_window_resized();
				break;
			case window_event_type::SizeChanged:
				surface_window().native_window_resized();
				break;
			case window_event_type::Enter:
				iNonClientEntered = false;
				surface_window().native_window_mouse_entered(windowEvent.position());
				break;
			case window_event_type::Leave:
				surface_window().native_window_mouse_left();
				break;
			case window_event_type::NonClientEnter:
				iNonClientEntered = true;
				surface_window().native_window_mouse_entered(windowEvent.position());
				break;
			case window_event_type::NonClientLeave:
				iNonClientEntered = false;
				surface_window().native_window_mouse_left();
				break;
			case window_event_type::FocusGained:
				surface_window().native_window_focus_gained();
				break;
			case window_event_type::FocusLost:
				surface_window().native_window_focus_lost();
				break;
			case window_event_type::TitleTextChanged:
				surface_window().native_window_title_text_changed(title_text());
				break;
			default:
				/* do nothing */
				break;
			}
		}
		else if (iCurrentEvent.is<mouse_event>())
		{
			const auto& mouseEvent = static_variant_cast<const mouse_event&>(iCurrentEvent);
			switch (mouseEvent.type())
			{
			case mouse_event_type::WheelScrolled:
				surface_window().native_window_mouse_wheel_scrolled(mouseEvent.mouse_wheel(), mouseEvent.delta());
				break;
			case mouse_event_type::ButtonPressed:
				surface_window().native_window_mouse_button_pressed(mouseEvent.mouse_button(), mouseEvent.position(), mouseEvent.key_modifiers());
				break;
			case mouse_event_type::ButtonDoubleClicked:
				surface_window().native_window_mouse_button_double_clicked(mouseEvent.mouse_button(), mouseEvent.position(), mouseEvent.key_modifiers());
				break;
			case mouse_event_type::ButtonReleased:
				surface_window().native_window_mouse_button_released(mouseEvent.mouse_button(), mouseEvent.position());
				break;
			case mouse_event_type::Moved:
				surface_window().native_window_mouse_moved(mouseEvent.position());
				break;
			default:
				/* do nothing */
				break;
			}
		}
		else if (iCurrentEvent.is<non_client_mouse_event>())
		{
			const auto& mouseEvent = static_variant_cast<const non_client_mouse_event&>(iCurrentEvent);
			switch (mouseEvent.type())
			{
			case mouse_event_type::WheelScrolled:
				surface_window().native_window_non_client_mouse_wheel_scrolled(mouseEvent.mouse_wheel(), mouseEvent.delta());
				break;
			case mouse_event_type::ButtonPressed:
				surface_window().native_window_non_client_mouse_button_pressed(mouseEvent.mouse_button(), mouseEvent.position(), mouseEvent.key_modifiers());
				break;
			case mouse_event_type::ButtonDoubleClicked:
				surface_window().native_window_non_client_mouse_button_double_clicked(mouseEvent.mouse_button(), mouseEvent.position(), mouseEvent.key_modifiers());
				break;
			case mouse_event_type::ButtonReleased:
				surface_window().native_window_non_client_mouse_button_released(mouseEvent.mouse_button(), mouseEvent.position());
				break;
			case mouse_event_type::Moved:
				surface_window().native_window_non_client_mouse_moved(mouseEvent.position());
				break;
			default:
				/* do nothing */
				break;
			}
		}
		else if (iCurrentEvent.is<keyboard_event>())
		{
			auto& keyboard = app::instance().keyboard();
			const auto& keyboardEvent = static_variant_cast<const keyboard_event&>(iCurrentEvent);
			switch (keyboardEvent.type())
			{
			case keyboard_event_type::KeyPressed:
				if (!keyboard.grabber().key_pressed(keyboardEvent.scan_code(), keyboardEvent.key_code(), keyboardEvent.key_modifiers()))
				{
					keyboard.key_pressed.trigger(keyboardEvent.scan_code(), keyboardEvent.key_code(), keyboardEvent.key_modifiers());
					surface_window().native_window_key_pressed(keyboardEvent.scan_code(), keyboardEvent.key_code(), keyboardEvent.key_modifiers());
				}
				break;
			case keyboard_event_type::KeyReleased:
				if (!keyboard.grabber().key_released(keyboardEvent.scan_code(), keyboardEvent.key_code(), keyboardEvent.key_modifiers()))
				{
					keyboard.key_released.trigger(keyboardEvent.scan_code(), keyboardEvent.key_code(), keyboardEvent.key_modifiers());
					surface_window().native_window_key_released(keyboardEvent.scan_code(), keyboardEvent.key_code(), keyboardEvent.key_modifiers());
				}
				break;
			case keyboard_event_type::TextInput:
				if (!keyboard.grabber().text_input(keyboardEvent.text()))
				{
					keyboard.text_input.trigger(keyboardEvent.text());
					surface_window().native_window_text_input(keyboardEvent.text());
				}
				break;
			case keyboard_event_type::SysTextInput:
				if (!keyboard.grabber().sys_text_input(keyboardEvent.text()))
				{
					keyboard.sys_text_input.trigger(keyboardEvent.text());
					surface_window().native_window_sys_text_input(keyboardEvent.text());
				}
				break;
			default:
				/* do nothing */
				break;
			}
		}
		if (destroyed)
			sc.ignore();
	}

	bool native_window::processing_event() const
	{
		return iProcessingEvent != 0;
	}

	bool native_window::has_rendering_priority() const
	{
		if (surface_window().native_window_has_rendering_priority())
			return true;
		uint32_t surfacesThatCanRender = 0;
		for (std::size_t i = 0; i < surface_manager().surface_count(); ++i)
			if (surface_manager().surface(i).has_native_surface() && surface_manager().surface(i).native_surface().can_render())
				++surfacesThatCanRender;
		return surfacesThatCanRender == 1 && can_render();
	}

	const std::string& native_window::title_text() const
	{
		return iTitleText;
	}

	void native_window::set_title_text(const std::string& aTitleText)
	{
		iTitleText = aTitleText;
	}

	i_rendering_engine& native_window::rendering_engine() const
	{
		return iRenderingEngine;
	}

	i_surface_manager& native_window::surface_manager() const
	{
		return iSurfaceManager;
	}

	bool native_window::non_client_entered() const
	{
		return iNonClientEntered;
	}

	size& native_window::pixel_density() const
	{
		if (iPixelDensityDpi == boost::none)
		{
			auto& display = app::instance().surface_manager().display(surface_window());
			iPixelDensityDpi = size{ display.metrics().horizontal_dpi(), display.metrics().vertical_dpi() };
		}
		return *iPixelDensityDpi;
	}

	void native_window::handle_dpi_changed()
	{
		app::instance().surface_manager().display(surface_window()).update_dpi();
		iPixelDensityDpi = boost::none;
		surface_window().handle_dpi_changed();
	}
}