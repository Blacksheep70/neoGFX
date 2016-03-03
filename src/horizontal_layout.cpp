// horizontal_layout.cpp
/*
  neogfx C++ GUI Library
  Copyright(C) 2016 Leigh Johnston
  
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

#include "neogfx.hpp"
#include <unordered_map>
#include <unordered_set>
#include <boost/pool/pool_alloc.hpp>
#include <neolib/bresenham_counter.hpp>
#include "horizontal_layout.hpp"
#include "i_widget.hpp"
#include "spacer.hpp"

namespace neogfx
{
	horizontal_layout::horizontal_layout(alignment aVerticalAlignment) :
		iVerticalAlignment(aVerticalAlignment)
	{
	}

	horizontal_layout::horizontal_layout(i_widget& aParent, alignment aVerticalAlignment) :
		layout(aParent), iVerticalAlignment(aVerticalAlignment)
	{
	}

	horizontal_layout::horizontal_layout(i_layout& aParent, alignment aVerticalAlignment) :
		layout(aParent), iVerticalAlignment(aVerticalAlignment)
	{
	}

	i_spacer& horizontal_layout::add_spacer()
	{
		auto s = std::make_shared<horizontal_spacer>();
		add_spacer(s);
		return *s;
	}

	i_spacer& horizontal_layout::add_spacer(uint32_t aPosition)
	{
		auto s = std::make_shared<horizontal_spacer>();
		add_spacer(aPosition, s);
		return *s;
	}

	size horizontal_layout::minimum_size() const
	{
		uint32_t itemsVisible = items_visible();
		if (itemsVisible == 0)
			return size{};
		size result;
		uint32_t itemsZeroSized = 0;
		for (const auto& item : items())
		{
			if (!item.visible())
				continue;
			if (!item.get().is<item::spacer_pointer>() && (item.minimum_size().cx == 0.0 || item.minimum_size().cy == 0.0))
			{
				++itemsZeroSized;
				continue;
			}
			result.cy = std::max(result.cy, item.minimum_size().cy);
			result.cx += item.minimum_size().cx;
		}
		result.cx += (margins().left + margins().right);
		result.cy += (margins().top + margins().bottom);
		if (result.cx != std::numeric_limits<size::dimension_type>::max() && (itemsVisible - itemsZeroSized) > 0)
			result.cx += (spacing().cx * (itemsVisible - itemsZeroSized - 1));
		result.cx = std::max(result.cx, layout::minimum_size().cx);
		result.cy = std::max(result.cy, layout::minimum_size().cy);
		return result;
	}

	size horizontal_layout::maximum_size() const
	{
		if (items_visible(static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout | ItemTypeSpacer)) == 0)
			return size{ std::numeric_limits<size::dimension_type>::max(), std::numeric_limits<size::dimension_type>::max() };
		uint32_t itemsVisible = items_visible();
		size result{ std::numeric_limits<size::dimension_type>::max(), 0.0 };
		for (const auto& item : items())
		{
			if (!item.visible())
				continue;
			result.cy = std::max(result.cy, item.maximum_size().cy);
			auto cx = std::min(result.cx, item.maximum_size().cx);
			if (cx != std::numeric_limits<size::dimension_type>::max())
				result.cx += cx;
			else
				result.cx = std::numeric_limits<size::dimension_type>::max();
		}
		if (result.cx != std::numeric_limits<size::dimension_type>::max())
			result.cx += (margins().left + margins().right);
		if (result.cy != std::numeric_limits<size::dimension_type>::max())
			result.cy += (margins().top + margins().bottom);
		if (result.cx != std::numeric_limits<size::dimension_type>::max() && itemsVisible > 1)
			result.cx += (spacing().cx * (itemsVisible - 1 - spacer_count()));
		if (result.cx != std::numeric_limits<size::dimension_type>::max())
			result.cx = std::min(result.cx, layout::maximum_size().cx);
		if (result.cy != std::numeric_limits<size::dimension_type>::max())
			result.cy = std::min(result.cy, layout::maximum_size().cy);
		return result;
	}

	void horizontal_layout::layout_items(const point& aPosition, const size& aSize)
	{
		if (!enabled())
			return;
		uint32_t itemsVisibleIncludingSpacers = items_visible(static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout | ItemTypeSpacer));
		if (itemsVisibleIncludingSpacers == 0)
			return;
		uint32_t itemsVisible = items_visible();
		owner()->layout_items_started();
		size availableSize = aSize;
		availableSize.cx -= (margins().left + margins().right);
		availableSize.cy -= (margins().top + margins().bottom);
		uint32_t itemsZeroSized = 0;
		if (aSize.cx <= minimum_size().cx || items_visible(ItemTypeSpacer))
		{
			for (const auto& item : items())
			{
				if (!item.visible())
					continue;
				if (!item.get().is<item::spacer_pointer>() && (item.minimum_size().cx == 0.0 || item.minimum_size().cy == 0.0))
					++itemsZeroSized;
			}
		}
		if (itemsVisible - itemsZeroSized > 1)
			availableSize.cx -= (spacing().cx * (itemsVisible - itemsZeroSized - 1));
		size::dimension_type leftover = availableSize.cx;
		size::dimension_type eachLeftover = std::floor(leftover / itemsVisible);
		size totalExpanderWeight;
		enum disposition_e { Unknown, Normal, TooSmall, TooBig, FixedSize };
		std::unordered_map<const item*, disposition_e, std::hash<const item*>, std::equal_to<const item*>, boost::pool_allocator<std::pair<const item*, disposition_e>>> itemDispositions;
		std::unordered_set<const item*, std::hash<const item*>, std::equal_to<const item*>, boost::pool_allocator<const item*>> expandersUsingLeftover;
		std::size_t itemsNotUsingLeftover = 0;
		bool done = false;
		while (!done)
		{
			done = true;
			for (const auto& item : items())
			{
				if (!item.visible())
					continue;
				if (expandersUsingLeftover.find(&item) != expandersUsingLeftover.end())
					continue;
				bool wasItemUsingLeftOver = (itemDispositions[&item] == Unknown || itemDispositions[&item] == Normal);
				if (item.size_policy() == size_policy::Expanding && item.maximum_size().cx >= leftover)
				{
					if (expandersUsingLeftover.empty())
					{
						itemDispositions.clear();
						itemsNotUsingLeftover = 0;
						leftover = availableSize.cx;
						totalExpanderWeight = size{};
						eachLeftover = 0.0;
					}
					expandersUsingLeftover.insert(&item);
					totalExpanderWeight += item.weight();
					done = false;
					break;
				}
				else if (item.size_policy() != size_policy::Expanding && !expandersUsingLeftover.empty())
				{
					if (itemDispositions[&item] != TooBig)
					{
						if (itemDispositions[&item] == TooSmall)
							leftover += item.maximum_size().cx;
						itemDispositions[&item] = TooBig;
						if (wasItemUsingLeftOver)
							++itemsNotUsingLeftover;
						leftover -= item.minimum_size().cx;
						done = false;
					}
				}
				else if (item.maximum_size().cx < eachLeftover)
				{
					if (itemDispositions[&item] != TooSmall && itemDispositions[&item] != Normal && itemDispositions[&item] != FixedSize)
					{
						if (itemDispositions[&item] == TooBig)
							leftover += item.minimum_size().cx;
						itemDispositions[&item] = item.is_fixed_size() ? FixedSize : TooSmall;
						if (wasItemUsingLeftOver)
							++itemsNotUsingLeftover;
						leftover -= item.maximum_size().cx;
						if (expandersUsingLeftover.empty())
							eachLeftover = std::floor(leftover / (itemsVisible - itemsNotUsingLeftover));
						done = false;
					}
				}
				else if (item.minimum_size().cx > eachLeftover)
				{
					if (itemDispositions[&item] != TooBig && itemDispositions[&item] != FixedSize)
					{
						if (itemDispositions[&item] == TooSmall)
							leftover += item.maximum_size().cx;
						itemDispositions[&item] = item.is_fixed_size() ? FixedSize : TooBig;
						if (wasItemUsingLeftOver)
							++itemsNotUsingLeftover;
						leftover -= item.minimum_size().cx;
						if (expandersUsingLeftover.empty())
							eachLeftover = std::floor(leftover / (itemsVisible - itemsNotUsingLeftover));
						done = false;
					}
				}
				else if (itemDispositions[&item] != Normal && itemDispositions[&item] != FixedSize)
				{
					if (itemDispositions[&item] == TooSmall)
						leftover += item.maximum_size().cx;
					else if (itemDispositions[&item] == TooBig)
						leftover += item.minimum_size().cx;
					itemDispositions[&item] = item.is_fixed_size() ? FixedSize : Normal;
					if (wasItemUsingLeftOver && item.is_fixed_size())
						++itemsNotUsingLeftover;
					else if (!wasItemUsingLeftOver && !item.is_fixed_size())
						--itemsNotUsingLeftover;
					if (expandersUsingLeftover.empty())
						eachLeftover = std::floor(leftover / (itemsVisible - itemsNotUsingLeftover));
					done = false;
				}
			}
		}
		if (leftover < 0.0)
		{
			leftover = 0.0;
			eachLeftover = 0.0;
		}
		uint32_t numberUsingLeftover = itemsVisibleIncludingSpacers - itemsNotUsingLeftover;
		uint32_t bitsLeft = static_cast<int32_t>(leftover - (eachLeftover * numberUsingLeftover));
		if (!expandersUsingLeftover.empty())
		{
			size::dimension_type totalIntegralAmount = 0.0;
			for (const auto& s : expandersUsingLeftover)
				totalIntegralAmount += std::floor(s->weight().cx / totalExpanderWeight.cx * leftover);
			bitsLeft = static_cast<int32_t>(leftover - totalIntegralAmount);
		}
		neolib::bresenham_counter<int32_t> bits(bitsLeft, numberUsingLeftover);
		uint32_t previousBit = 0;
		point nextPos = aPosition;
		nextPos.x += margins().left;
		nextPos.y += margins().top;
		for (auto& item : items())
		{
			if (!item.visible())
				continue;
			size s{ 0, std::min(std::max(item.minimum_size().cy, availableSize.cy), item.maximum_size().cy) };
			point alignmentAdjust;
			switch (iVerticalAlignment)
			{
			case alignment::Top:
				alignmentAdjust.y = 0.0;
				break;
			case alignment::Bottom:
				alignmentAdjust.y = availableSize.cy - s.cy;
				break;
			case alignment::VCentre:
			default:
				alignmentAdjust.y = std::ceil((availableSize.cy - s.cy) / 2.0);
				break;
			}
			if (alignmentAdjust.y < 0.0)
				alignmentAdjust.y = 0.0;
			if (itemDispositions[&item] == TooBig || itemDispositions[&item] == FixedSize)
				s.cx = item.minimum_size().cx;
			else if (itemDispositions[&item] == TooSmall)
				s.cx = item.maximum_size().cx;
			else if (expandersUsingLeftover.find(&item) != expandersUsingLeftover.end())
			{
				uint32_t bit = bitsLeft != 0 ? bits() : 0;
				s.cx = std::floor(item.weight().cx / totalExpanderWeight.cx * leftover) + static_cast<size::dimension_type>(bit - previousBit);
				previousBit = bit;
			}
			else
			{
				uint32_t bit = bitsLeft != 0 ? bits() : 0;
				s.cx = eachLeftover + static_cast<size::dimension_type>(bit - previousBit);
				previousBit = bit;
			}
			item.layout(nextPos + alignmentAdjust, s);
			if (!item.get().is<item::spacer_pointer>() && (s.cx == 0.0 || s.cy == 0.0))
				continue;
			nextPos.x += s.cx;
			if (!item.get().is<item::spacer_pointer>())
				nextPos.x += spacing().cx;
		}
		owner()->layout_items_completed();
	}
}