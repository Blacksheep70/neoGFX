// layout.inl
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

#include <unordered_map>
#include <unordered_set>

namespace neogfx
{
	template <typename SpecializedPolicy>
	struct layout::common_axis_policy
	{
		static uint32_t items_visible(layout& aLayout, layout::item_type_e aItemType = static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout))
		{
			return aLayout.items_visible(aItemType);
		}
		static uint32_t items_zero_sized(layout& aLayout, const size& aSize)
		{
			uint32_t result = 0;
			if (SpecializedPolicy::cx(aSize) <= SpecializedPolicy::cx(aLayout.minimum_size()) || SpecializedPolicy::items_visible(aLayout, ItemTypeSpacer))
			{
				for (const auto& item : aLayout.items())
				{
					if (!item.visible())
						continue;
					if (!item.get().is<item::spacer_pointer>() && (SpecializedPolicy::cx(item.minimum_size()) == 0.0 || SpecializedPolicy::cy(item.minimum_size()) == 0.0))
						++result;
				}
			}
			return result;
		}
	};

	template <typename Layout>
	struct layout::column_major : common_axis_policy<column_major<Layout>>
	{
		typedef Layout layout_type;
		static const neogfx::alignment AlignmentMask = neogfx::alignment::Top | neogfx::alignment::VCentre | neogfx::alignment::Bottom;
		static const point::coordinate_type& x(const point& aPoint) { return aPoint.x; }
		static point::coordinate_type& x(point& aPoint) { return aPoint.x; }
		static const point::coordinate_type& y(const point& aPoint) { return aPoint.y; }
		static point::coordinate_type& y(point& aPoint) { return aPoint.y; }
		static const size::dimension_type& cx(const size& aSize) { return aSize.cx; }
		static size::dimension_type& cx(size& aSize) { return aSize.cx; }
		static const size::dimension_type& cy(const size& aSize) { return aSize.cy; }
		static size::dimension_type& cy(size& aSize) { return aSize.cy; }
		static neogfx::size_policy::size_policy_e size_policy_x(const neogfx::size_policy& aSizePolicy) { return aSizePolicy.horizontal_size_policy(); }
		static neogfx::size_policy::size_policy_e size_policy_y(const neogfx::size_policy& aSizePolicy) { return aSizePolicy.vertical_size_policy(); }
	};

	template <typename Layout>
	struct layout::row_major : common_axis_policy<row_major<Layout>>
	{
		typedef Layout layout_type;
		static const neogfx::alignment AlignmentMask = neogfx::alignment::Left | neogfx::alignment::Centre | neogfx::alignment::Right;
		static const point::coordinate_type& x(const point& aPoint) { return aPoint.y; }
		static point::coordinate_type& x(point& aPoint) { return aPoint.y; }
		static const point::coordinate_type& y(const point& aPoint) { return aPoint.x; }
		static point::coordinate_type& y(point& aPoint) { return aPoint.x; }
		static const size::dimension_type& cx(const size& aSize) { return aSize.cy; }
		static size::dimension_type& cx(size& aSize) { return aSize.cy; }
		static const size::dimension_type& cy(const size& aSize) { return aSize.cx; }
		static size::dimension_type& cy(size& aSize) { return aSize.cx; }
		static neogfx::size_policy::size_policy_e size_policy_x(const neogfx::size_policy& aSizePolicy) { return aSizePolicy.vertical_size_policy(); }
		static neogfx::size_policy::size_policy_e size_policy_y(const neogfx::size_policy& aSizePolicy) { return aSizePolicy.horizontal_size_policy(); }
	};

	template <typename AxisPolicy>
	void layout::do_layout_items(const point& aPosition, const size& aSize)
	{
		set_position(aPosition);
		set_extents(aSize);
		auto itemsVisibleIncludingSpacers = AxisPolicy::items_visible(static_cast<typename AxisPolicy::layout_type&>(*this), static_cast<item_type_e>(ItemTypeWidget | ItemTypeLayout | ItemTypeSpacer));
		if (itemsVisibleIncludingSpacers == 0)
			return;
		uint32_t itemsVisible = AxisPolicy::items_visible(static_cast<typename AxisPolicy::layout_type&>(*this));
		bool haveSpacers = (itemsVisibleIncludingSpacers > itemsVisible);
		size availableSize = aSize;
		availableSize.cx -= (margins().left + margins().right);
		availableSize.cy -= (margins().top + margins().bottom);
		auto itemsZeroSized = AxisPolicy::items_zero_sized(static_cast<typename AxisPolicy::layout_type&>(*this), aSize);
		auto spaces = (iAlwaysUseSpacing ? itemsVisibleIncludingSpacers : itemsVisible) - itemsZeroSized;
		if (spaces > 1)
			AxisPolicy::cx(availableSize) -= (AxisPolicy::cx(spacing()) * (spaces - 1));
		size::dimension_type leftover = AxisPolicy::cx(availableSize);
		uint32_t itemsUsingLeftover = 0;
		size totalExpanderWeight;
		enum disposition_e { Unknown, Weighted, Unweighted, TooSmall, FixedSize };
		std::unordered_map<const item*, disposition_e, std::hash<const item*>, std::equal_to<const item*>, boost::pool_allocator<std::pair<const item*, disposition_e>>> itemDispositions;
		for (const auto& item : items())
		{
			if (!item.visible())
				continue;
			if (AxisPolicy::size_policy_x(item.size_policy()) == neogfx::size_policy::Minimum)
			{
				itemDispositions[&item] = TooSmall;
				leftover -= AxisPolicy::cx(item.minimum_size());
				if (leftover < 0.0)
					leftover = 0.0;
			}
			else if (AxisPolicy::size_policy_x(item.size_policy()) == neogfx::size_policy::Fixed)
			{
				itemDispositions[&item] = FixedSize;
				leftover -= AxisPolicy::cx(item.minimum_size());
				if (leftover < 0.0)
					leftover = 0.0;
			}
			else
			{
				++itemsUsingLeftover;
				totalExpanderWeight += item.weight();
			}
		}
		bool done = false;
		while (!done && itemsUsingLeftover > 0)
		{
			done = true;
			for (const auto& item : items())
			{
				if (!item.visible())
					continue;
				auto& disposition = itemDispositions[&item];
				if (disposition != Unknown && disposition != Weighted)
					continue;
				auto minSize = AxisPolicy::cx(item.minimum_size());
				auto maxSize = AxisPolicy::cx(item.maximum_size());
				auto weightedSize = AxisPolicy::cx(item.weight()) / AxisPolicy::cx(totalExpanderWeight) * leftover;
				if (minSize < weightedSize && maxSize > weightedSize)
					disposition = Weighted;
				else
				{
					disposition = maxSize <= weightedSize ? TooSmall : Unweighted;
					leftover -= AxisPolicy::cx(disposition == TooSmall ? item.maximum_size() : item.minimum_size());
					if (leftover < 0.0)
						leftover = 0.0;
					totalExpanderWeight -= item.weight();
					if (--itemsUsingLeftover == 0)
						break;
					done = false;
				}
			}
		}
		size::dimension_type weightedAmount = 0.0;
		if (AxisPolicy::cx(totalExpanderWeight) > 0.0)
			for (const auto& item : items())
				if (item.visible() && itemDispositions[&item] == Weighted)
					weightedAmount += std::floor(AxisPolicy::cx(item.weight()) / AxisPolicy::cx(totalExpanderWeight) * leftover);
		uint32_t bitsLeft = 0;
		if (itemsUsingLeftover > 0)
			bitsLeft = static_cast<int32_t>(leftover - weightedAmount);
		neolib::bresenham_counter<int32_t> bits(bitsLeft, itemsUsingLeftover);
		uint32_t previousBit = 0;
		point nextPos = aPosition;
		nextPos.x += margins().left;
		nextPos.y += margins().top;
		for (auto& item : items())
		{
			if (!item.visible())
				continue;
			size s{0.0, std::min(std::max(AxisPolicy::cy(item.minimum_size()), AxisPolicy::cy(availableSize)), AxisPolicy::cy(item.maximum_size()))};
			std::swap(s.cx, AxisPolicy::cx(s));
			point alignmentAdjust;
			switch (alignment() & AxisPolicy::AlignmentMask)
			{
			case alignment::Left:
			case alignment::Top:
			default:
				AxisPolicy::y(alignmentAdjust) = 0.0;
				break;
			case alignment::Right:
			case alignment::Bottom:
				AxisPolicy::y(alignmentAdjust) = AxisPolicy::cy(availableSize) - AxisPolicy::cy(s);
				break;
			case alignment::Centre:
			case alignment::VCentre:
				AxisPolicy::y(alignmentAdjust) = std::ceil((AxisPolicy::cy(availableSize) - AxisPolicy::cy(s)) / 2.0);
				break;
			}
			if (AxisPolicy::y(alignmentAdjust) < 0.0)
				AxisPolicy::y(alignmentAdjust) = 0.0;
			auto disposition = itemDispositions[&item];
			if (disposition == FixedSize)
				AxisPolicy::cx(s) = AxisPolicy::cx(item.minimum_size());
			else if (disposition == TooSmall)
				AxisPolicy::cx(s) = AxisPolicy::cx(AxisPolicy::size_policy_x(item.size_policy()) == neogfx::size_policy::Minimum ? item.minimum_size() : item.maximum_size());
			else if (disposition == Weighted && leftover > 0.0)
			{
				uint32_t bit = bitsLeft != 0 ? bits() : 0;
				AxisPolicy::cx(s) = std::floor(AxisPolicy::cx(item.weight()) / AxisPolicy::cx(totalExpanderWeight) * leftover) + static_cast<size::dimension_type>(bit - previousBit);
				previousBit = bit;
			}
			else
				AxisPolicy::cx(s) = AxisPolicy::cx(item.minimum_size());
			item.layout(nextPos + alignmentAdjust, s);
			if (!item.get().is<item::spacer_pointer>() && (AxisPolicy::cx(s) == 0.0 || AxisPolicy::cy(s) == 0.0))
				continue;
			AxisPolicy::x(nextPos) += AxisPolicy::cx(s);
			if (!item.get().is<item::spacer_pointer>() || iAlwaysUseSpacing)
				AxisPolicy::x(nextPos) += AxisPolicy::cx(spacing());
		}
	}
}
