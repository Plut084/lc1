#pragma once

namespace lc1 {

class Character;

class Interactable {
  public:
    Interactable() = default;
    Interactable(Interactable const &) = delete;
    Interactable(Interactable &&) = delete;
    Interactable &operator=(Interactable const &) = delete;
    Interactable &operator=(Interactable &&) = delete;

    virtual ~Interactable() = default;
    virtual void interact(Character &actor) = 0;
    virtual bool can_interact(Character &actor) const = 0;
};

class InteractableCharacter : public Interactable {
  public:
    InteractableCharacter(Character *character) : me_(character) {}

    InteractableCharacter(InteractableCharacter const &) = delete;
    InteractableCharacter(InteractableCharacter &&) = delete;
    InteractableCharacter &operator=(InteractableCharacter const &) = delete;
    InteractableCharacter &operator=(InteractableCharacter &&) = delete;

    ~InteractableCharacter() override = default;

    void interact(Character &actor) override;
    bool can_interact(Character &actor) const override;

  private:
    Character *me_;
};

} // namespace lc1
