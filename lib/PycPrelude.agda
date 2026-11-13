{-# OPTIONS --erasure --safe #-}
-- The fixed Agda definitions that every checked Python file is translated against.
-- Each name a checked file can import from `dependent` has its meaning here.
-- Names with a subscript digit or a symbol are internal: Python cannot spell them.
-- Type arguments are marked @0 (erased): they exist for checking only, which lets
-- them be filled in from the square-bracket names of a checked function.
module PycPrelude where
open import Agda.Primitive using (Level; _⊔_)
open import Agda.Builtin.Nat public using (Nat; zero; suc; _+_; _*_)
open import Agda.Builtin.Bool public using (Bool; true; false)
open import Agda.Builtin.String public using (String)
open import Agda.Builtin.Equality public using (_≡_; refl)
import Agda.Builtin.Nat as N
import Agda.Builtin.String as S

the : ∀ {ℓ} (@0 A : Set ℓ) → A → A
the A x = x

if_then_else_ : ∀ {ℓ} {@0 A : Set ℓ} → Bool → A → A → A
if true then a else b = a
if false then a else b = b

not : Bool → Bool
not true = false
not false = true
_∧_ : Bool → Bool → Bool
true ∧ b = b
false ∧ b = false
_∨_ : Bool → Bool → Bool
true ∨ b = true
false ∨ b = b

-- Callable[[A, B], C] is Fun₂ A B C. A record per argument count keeps the count in
-- the type, so calling a two-argument function with one argument is a type error,
-- as it is an error when the program runs.
record Fun₀ {a} (A : Set a) : Set a where
  constructor fun₀
  field call₀ : A
open Fun₀ public
record Fun₁ {a b} (A : Set a) (B : Set b) : Set (a ⊔ b) where
  constructor fun₁
  field call₁ : A → B
open Fun₁ public
record Fun₂ {a b c} (A : Set a) (B : Set b) (C : Set c) : Set (a ⊔ b ⊔ c) where
  constructor fun₂
  field call₂ : A → B → C
open Fun₂ public
record Fun₃ {a b c d} (A : Set a) (B : Set b) (C : Set c) (D : Set d) : Set (a ⊔ b ⊔ c ⊔ d) where
  constructor fun₃
  field call₃ : A → B → C → D
open Fun₃ public
record Fun₄ {a b c d e} (A : Set a) (B : Set b) (C : Set c) (D : Set d) (E : Set e) : Set (a ⊔ b ⊔ c ⊔ d ⊔ e) where
  constructor fun₄
  field call₄ : A → B → C → D → E
open Fun₄ public

record Eq₀ (A : Set) : Set where
  field _==_ : A → A → Bool
open Eq₀ {{...}} public
instance
  EqNat : Eq₀ Nat
  EqNat = record { _==_ = N._==_ }
  EqBool : Eq₀ Bool
  EqBool = record { _==_ = λ { true b → b ; false b → not b } }
  EqString : Eq₀ String
  EqString = record { _==_ = S.primStringEquality }
-- Error messages show a comparison as written, without the instance that was chosen.
{-# DISPLAY Eq₀._==_ _ a b = a == b #-}

_<_ : Nat → Nat → Bool
_<_ = N._<_
_<=_ : Nat → Nat → Bool
a <= b = not (b < a)

data ⊥ : Set where
absurd : ∀ {ℓ} {@0 A : Set ℓ} → ⊥ → A
absurd ()

sym : ∀ {ℓ} {@0 A : Set ℓ} {@0 x y : A} → x ≡ y → y ≡ x
sym refl = refl
trans : ∀ {ℓ} {@0 A : Set ℓ} {@0 x y z : A} → x ≡ y → y ≡ z → x ≡ z
trans refl q = q
cong : ∀ {a b} {@0 A : Set a} {@0 B : Set b} {@0 x y : A} (f : Fun₁ A B) → x ≡ y → call₁ f x ≡ call₁ f y
cong f refl = refl
subst : ∀ {a b} {@0 A : Set a} {@0 x y : A} (P : Fun₁ A (Set b)) → x ≡ y → call₁ P x → call₁ P y
subst P refl px = px
pred : Nat → Nat
pred zero = zero
pred (suc n) = n
monus : Nat → Nat → Nat
monus = N._-_
concat : String → String → String
concat = S.primStringAppend
