#include "lc1/game/party.hpp"

#include "lc1/game/character.hpp"

namespace lc1 {

std::vector<DrawItem> Party::draw_items() const
{
    std::vector<DrawItem> items;
    items.reserve(members_.size() + (leader_ == nullptr ? 0U : 1U));
    // A party without a leader is still drawable; it is only its characters that must
    // carry a mesh and material.
    if (leader_ != nullptr)
        items.push_back(leader_->draw_item());
    for (auto const *member : members_) {
        items.push_back(member->draw_item());
    }

    return items;
}

} // namespace lc1
