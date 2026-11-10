# Network synchronization

Three classes, for example of `Party`:

- PartyCore, client side, non-virtual
- Party : public PartyCore, server side; inherites only methods, no polymorphism
- PartyUpdate, for synchronization between `Party` and `PartyCore`
