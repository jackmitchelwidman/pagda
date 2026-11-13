{-# OPTIONS --erasure --safe #-}
-- The fixed Agda definitions that every checked Python file is translated against.
-- Each name a checked file can import from `dependent` has its meaning here.
-- Names with a subscript digit or a symbol are internal: Python cannot spell them.
-- Type arguments are marked @0 (erased): they exist for checking only, which lets
-- them be filled in from the square-bracket names of a checked function.
module PagdaPrelude where
open import Agda.Primitive using (Level; _⊔_)
open import Agda.Builtin.Nat public using (Nat; zero; suc; _+_; _*_)
open import Agda.Builtin.Bool public using (Bool; true; false)
open import Agda.Builtin.String public using (String)
open import Agda.Builtin.Equality public using (_≡_; refl)
open import Agda.Builtin.Int public using (Int; pos; negsuc)
open import Agda.Builtin.List public using (List; []; _∷_)
open import Agda.Builtin.Sigma public using (Σ; _,_; fst; snd)
open import Agda.Builtin.Unit public using (⊤; tt)
open import Agda.Builtin.Maybe public using (Maybe; just; nothing)
import Agda.Builtin.Nat as N
import Agda.Builtin.String as S
import Agda.Builtin.Int as I

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

-- A function type with two arguments is Fun₂ A B C. A record per argument count
-- keeps the count in the type, so calling a two-argument function with one argument
-- is a type error, as it is an error when the program runs.
-- Each type after the first is a function of the arguments before it, so a later
-- type can mention an earlier argument:
--   Fn[k: Nat, P(k), Q(k)]   is   Fun₂ Nat (λ k → P k) (λ k _ → Q k)
--   Callable[[A, B], C]      is   Fun₂ A (λ _ → B) (λ _ _ → C)
record Fun₀ {a} (A : Set a) : Set a where
  constructor fun₀
  field call₀ : A
open Fun₀ public
record Fun₁ {a b} (A : Set a) (B : A → Set b) : Set (a ⊔ b) where
  constructor fun₁
  field call₁ : (x : A) → B x
open Fun₁ public
record Fun₂ {a b c} (A : Set a) (B : A → Set b) (C : (x : A) → B x → Set c) : Set (a ⊔ b ⊔ c) where
  constructor fun₂
  field call₂ : (x : A) (y : B x) → C x y
open Fun₂ public
record Fun₃ {a b c d} (A : Set a) (B : A → Set b) (C : (x : A) → B x → Set c)
            (D : (x : A) (y : B x) → C x y → Set d) : Set (a ⊔ b ⊔ c ⊔ d) where
  constructor fun₃
  field call₃ : (x : A) (y : B x) (z : C x y) → D x y z
open Fun₃ public
record Fun₄ {a b c d e} (A : Set a) (B : A → Set b) (C : (x : A) → B x → Set c)
            (D : (x : A) (y : B x) → C x y → Set d)
            (E : (x : A) (y : B x) (z : C x y) → D x y z → Set e) : Set (a ⊔ b ⊔ c ⊔ d ⊔ e) where
  constructor fun₄
  field call₄ : (x : A) (y : B x) (z : C x y) (w : D x y z) → E x y z w
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
-- Error messages show a comparison as written.
{-# DISPLAY N._<_ a b = a < b #-}
{-# DISPLAY N._==_ a b = a == b #-}
_<=_ : Nat → Nat → Bool
a <= b = not (b < a)

data ⊥ : Set where
absurd : ∀ {ℓ} {@0 A : Set ℓ} → ⊥ → A
absurd ()

sym : ∀ {ℓ} {@0 A : Set ℓ} {@0 x y : A} → x ≡ y → y ≡ x
sym refl = refl
trans : ∀ {ℓ} {@0 A : Set ℓ} {@0 x y z : A} → x ≡ y → y ≡ z → x ≡ z
trans refl q = q
cong : ∀ {a b} {@0 A : Set a} {@0 B : Set b} {@0 x y : A} (f : Fun₁ A (λ _ → B)) → x ≡ y → call₁ f x ≡ call₁ f y
cong f refl = refl
subst : ∀ {a b} {@0 A : Set a} {@0 x y : A} (P : Fun₁ A (λ _ → Set b)) → x ≡ y → call₁ P x → call₁ P y
subst P refl px = px

-- decide(c) says which value c has and carries the proof of it.
data Dec₀ (@0 b : Bool) : Set where
  Yes : b ≡ true → Dec₀ b
  No : b ≡ false → Dec₀ b
decide : (b : Bool) → Dec₀ b
decide true = Yes refl
decide false = No refl
pred : Nat → Nat
pred zero = zero
pred (suc n) = n
monus : Nat → Nat → Nat
monus = N._-_
{-# DISPLAY N._-_ a b = monus a b #-}
concat : String → String → String
concat = S.primStringAppend

-- ---------------------------------------------------------------------------
-- Facts about conditions. A test `if c:` hands each branch a proof of c ≡ true or
-- c ≡ false; these turn one form into another.
notFalse : {c : Bool} → c ≡ false → not c ≡ true
notFalse refl = refl
notTrue : {c : Bool} → c ≡ true → not c ≡ false
notTrue refl = refl
unNot : {c : Bool} → not c ≡ true → c ≡ false
unNot {false} p = refl
-- if with a proof for each branch, for use inside an expression
ifd : ∀ {ℓ} {@0 A : Set ℓ} (c : Bool) → (c ≡ true → A) → (c ≡ false → A) → A
ifd true a b = a refl
ifd false a b = b refl
case₀_of_ : ∀ {a b} {@0 A : Set a} {@0 B : Set b} → A → (A → B) → B
case₀ x of f = f x

-- ---------------------------------------------------------------------------
-- int: whole numbers of either sign. pos n is n, negsuc n is -(n + 1).
natSub : Nat → Nat → Int
natSub n zero = pos n
natSub zero (suc m) = negsuc m
natSub (suc n) (suc m) = natSub n m
negᵢ : Int → Int
negᵢ (pos zero) = pos zero
negᵢ (pos (suc n)) = negsuc n
negᵢ (negsuc n) = pos (suc n)
_+ᵢ_ : Int → Int → Int
pos a +ᵢ pos b = pos (a + b)
pos a +ᵢ negsuc b = natSub a (suc b)
negsuc a +ᵢ pos b = natSub b (suc a)
negsuc a +ᵢ negsuc b = negsuc (suc (a + b))
_-ᵢ_ : Int → Int → Int
a -ᵢ b = a +ᵢ negᵢ b
_*ᵢ_ : Int → Int → Int
pos a *ᵢ pos b = pos (a * b)
pos a *ᵢ negsuc b = negᵢ (pos (a * suc b))
negsuc a *ᵢ pos b = negᵢ (pos (suc a * b))
negsuc a *ᵢ negsuc b = pos (suc a * suc b)
_<ᵢ_ : Int → Int → Bool
pos a <ᵢ pos b = a < b
pos a <ᵢ negsuc b = false
negsuc a <ᵢ pos b = true
negsuc a <ᵢ negsuc b = b < a
_<=ᵢ_ : Int → Int → Bool
a <=ᵢ b = not (b <ᵢ a)
absᵢ : Int → Nat
absᵢ (pos n) = n
absᵢ (negsuc n) = suc n
-- to_nat(x): x when it is 0 or more, else 0
toNat : Int → Nat
toNat (pos n) = n
toNat (negsuc n) = zero
eqInt : Int → Int → Bool
eqInt (pos a) (pos b) = a N.== b
eqInt (negsuc a) (negsuc b) = a N.== b
eqInt _ _ = false
instance
  EqInt : Eq₀ Int
  EqInt = record { _==_ = eqInt }
{-# DISPLAY eqInt a b = a == b #-}

-- // and % as Python defines them: the quotient is rounded down, and the remainder
-- has the sign of the divisor. Each takes a proof that the divisor is not 0.
divNat : (a b : Nat) → (b == 0) ≡ false → Nat
divNat a zero ()
divNat a (suc m) p = N.div-helper 0 m a m
modNat : (a b : Nat) → (b == 0) ≡ false → Nat
modNat a zero ()
modNat a (suc m) p = N.mod-helper 0 m a m
divInt : (a b : Int) → (b == pos 0) ≡ false → Int
divInt a (pos zero) ()
divInt (pos n) (pos (suc m)) p = pos (N.div-helper 0 m n m)
divInt (pos n) (negsuc m) p = negᵢ (pos (N.div-helper 0 m (n + m) m))
divInt (negsuc n) (pos (suc m)) p = negᵢ (pos (N.div-helper 0 m (suc n + m) m))
divInt (negsuc n) (negsuc m) p = pos (N.div-helper 0 m (suc n) m)
modInt : (a b : Int) → (b == pos 0) ≡ false → Int
modInt a b p = a -ᵢ (b *ᵢ divInt a b p)
powNat : Nat → Nat → Nat
powNat a zero = 1
powNat a (suc n) = a * powNat a n
powInt : Int → Nat → Int
powInt a zero = pos 1
powInt a (suc n) = a *ᵢ powInt a n
minNat : Nat → Nat → Nat
minNat a b = if b < a then b else a
maxNat : Nat → Nat → Nat
maxNat a b = if a < b then b else a
minInt : Int → Int → Int
minInt a b = if b <ᵢ a then b else a
maxInt : Int → Int → Int
maxInt a b = if a <ᵢ b then b else a
showNat : Nat → String
showNat = S.primShowNat
showInt : Int → String
showInt = I.primShowInteger

-- ---------------------------------------------------------------------------
-- tuple[A, B] is A × B. A longer tuple nests to the right: (a , (b , c)).
_×_ : ∀ {a b} → Set a → Set b → Set (a ⊔ b)
A × B = Σ A (λ _ → B)
infixr 2 _×_
instance
  EqPair : ∀ {A B : Set} {{_ : Eq₀ A}} {{_ : Eq₀ B}} → Eq₀ (A × B)
  EqPair = record { _==_ = λ { (a , b) (c , d) → (a == c) ∧ (b == d) } }
  EqUnit : Eq₀ ⊤
  EqUnit = record { _==_ = λ _ _ → true }

-- ---------------------------------------------------------------------------
-- list[A]
len : ∀ {a} {@0 A : Set a} → List A → Nat
len [] = 0
len (x ∷ xs) = suc (len xs)
_++_ : ∀ {a} {@0 A : Set a} → List A → List A → List A
[] ++ ys = ys
(x ∷ xs) ++ ys = x ∷ (xs ++ ys)
infixr 5 _++_
mapL : ∀ {a b} {@0 A : Set a} {@0 B : Set b} → (A → B) → List A → List B
mapL f [] = []
mapL f (x ∷ xs) = f x ∷ mapL f xs
concatMap : ∀ {a b} {@0 A : Set a} {@0 B : Set b} → (A → List B) → List A → List B
concatMap f [] = []
concatMap f (x ∷ xs) = f x ++ concatMap f xs
eqList : {A : Set} {{_ : Eq₀ A}} → List A → List A → Bool
eqList [] [] = true
eqList (x ∷ xs) (y ∷ ys) = (x == y) ∧ eqList xs ys
eqList _ _ = false
instance
  EqList : {A : Set} {{_ : Eq₀ A}} → Eq₀ (List A)
  EqList = record { _==_ = eqList }
elem : {A : Set} {{_ : Eq₀ A}} → A → List A → Bool
elem x [] = false
elem x (y ∷ ys) = (x == y) ∨ elem x ys
-- xs[i], with a proof that i is inside the list
index : ∀ {a} {@0 A : Set a} (xs : List A) (i : Nat) → (i < len xs) ≡ true → A
index [] i ()
index (x ∷ xs) zero p = x
index (x ∷ xs) (suc i) p = index xs i p
-- xs[i] = v
setAt : ∀ {a} {@0 A : Set a} (xs : List A) (i : Nat) → A → (i < len xs) ≡ true → List A
setAt [] i v ()
setAt (x ∷ xs) zero v p = v ∷ xs
setAt (x ∷ xs) (suc i) v p = x ∷ setAt xs i v p
take : ∀ {a} {@0 A : Set a} → Nat → List A → List A
take zero xs = []
take (suc n) [] = []
take (suc n) (x ∷ xs) = x ∷ take n xs
drop : ∀ {a} {@0 A : Set a} → Nat → List A → List A
drop zero xs = xs
drop (suc n) [] = []
drop (suc n) (x ∷ xs) = drop n xs
-- xs[a:b]
slice : ∀ {a} {@0 A : Set a} → Nat → Nat → List A → List A
slice a b xs = take (monus b a) (drop a xs)
-- the numbers a, a + 1, ... of which there are n
countFrom : Nat → Nat → List Nat
countFrom a zero = []
countFrom a (suc n) = a ∷ countFrom (suc a) n
range₁ : Nat → List Nat
range₁ n = countFrom 0 n
range₂ : Nat → Nat → List Nat
range₂ a b = countFrom a (monus b a)
revOnto : ∀ {a} {@0 A : Set a} → List A → List A → List A
revOnto acc [] = acc
revOnto acc (x ∷ xs) = revOnto (x ∷ acc) xs
reverse : ∀ {a} {@0 A : Set a} → List A → List A
reverse = revOnto []
enumFrom : ∀ {a} {@0 A : Set a} → Nat → List A → List (Nat × A)
enumFrom n [] = []
enumFrom n (x ∷ xs) = (n , x) ∷ enumFrom (suc n) xs
zip : ∀ {a b} {@0 A : Set a} {@0 B : Set b} → List A → List B → List (A × B)
zip (x ∷ xs) (y ∷ ys) = (x , y) ∷ zip xs ys
zip _ _ = []
sumNat : List Nat → Nat
sumNat [] = 0
sumNat (x ∷ xs) = x + sumNat xs
sumInt : List Int → Int
sumInt [] = pos 0
sumInt (x ∷ xs) = x +ᵢ sumInt xs
anyL : List Bool → Bool
anyL [] = false
anyL (x ∷ xs) = x ∨ anyL xs
allL : List Bool → Bool
allL [] = true
allL (x ∷ xs) = x ∧ allL xs
insertBy : ∀ {a} {@0 A : Set a} → (A → A → Bool) → A → List A → List A
insertBy lt x [] = x ∷ []
insertBy lt x (y ∷ ys) with lt x y
... | true = x ∷ y ∷ ys
... | false = y ∷ insertBy lt x ys
sortBy : ∀ {a} {@0 A : Set a} → (A → A → Bool) → List A → List A
sortBy lt [] = []
sortBy lt (x ∷ xs) = insertBy lt x (sortBy lt xs)
sortedNat : List Nat → List Nat
sortedNat = sortBy _<_
sortedInt : List Int → List Int
sortedInt = sortBy _<ᵢ_
strLen : String → Nat
strLen s = len (S.primStringToList s)

-- ---------------------------------------------------------------------------
-- dict[K, V]: keys in the order they were first added, each with its value.
record Dict (K V : Set) : Set where
  constructor mkDict
  field items : List (K × V)
open Dict public
hasKey : {K V : Set} {{_ : Eq₀ K}} → K → List (K × V) → Bool
hasKey k [] = false
hasKey k ((k′ , v) ∷ r) = (k == k′) ∨ hasKey k r
has : {K V : Set} {{_ : Eq₀ K}} → Dict K V → K → Bool
has d k = hasKey k (items d)
getOr : {K V : Set} {{_ : Eq₀ K}} → K → V → List (K × V) → V
getOr k v [] = v
getOr k v ((k′ , v′) ∷ r) with k == k′
... | true = v′
... | false = getOr k v r
dget : {K V : Set} {{_ : Eq₀ K}} → Dict K V → K → V → V
dget d k v = getOr k v (items d)
lookupKey : {K V : Set} {{_ : Eq₀ K}} (k : K) (r : List (K × V)) → hasKey k r ≡ true → V
lookupKey k [] ()
lookupKey k ((k′ , v) ∷ r) p with k == k′
... | true = v
... | false = lookupKey k r p
dindex : {K V : Set} {{_ : Eq₀ K}} (d : Dict K V) (k : K) → has d k ≡ true → V
dindex d k p = lookupKey k (items d) p
putKey : {K V : Set} {{_ : Eq₀ K}} → K → V → List (K × V) → List (K × V)
putKey k v [] = (k , v) ∷ []
putKey k v ((k′ , v′) ∷ r) with k == k′
... | true = (k′ , v) ∷ r
... | false = (k′ , v′) ∷ putKey k v r
dset : {K V : Set} {{_ : Eq₀ K}} → Dict K V → K → V → Dict K V
dset d k v = mkDict (putKey k v (items d))
-- a dict written out: later items with the same key replace the value and keep the place
dictFrom : {K V : Set} {{_ : Eq₀ K}} → Dict K V → List (K × V) → Dict K V
dictFrom d [] = d
dictFrom d ((k , v) ∷ r) = dictFrom (dset d k v) r
dictOf : {K V : Set} {{_ : Eq₀ K}} → List (K × V) → Dict K V
dictOf = dictFrom (mkDict [])
dlen : {K V : Set} → Dict K V → Nat
dlen d = len (items d)
dkeys : {K V : Set} → Dict K V → List K
dkeys d = mapL fst (items d)
dvalues : {K V : Set} → Dict K V → List V
dvalues d = mapL snd (items d)

-- ---------------------------------------------------------------------------
-- Loops. The names a loop assigns travel through it as one value, the state.
-- The body says what happens next: go on, leave the loop, or return from the function.
data Step {r s} (R : Set r) (S : Set s) : Set (r ⊔ s) where
  next : S → Step R S
  stop : S → Step R S
  ret : R → Step R S
data Out {r s} (R : Set r) (S : Set s) : Set (r ⊔ s) where
  out : S → Out R S
  early : R → Out R S
-- a loop with no return inside can only finish with its state
runLoop : ∀ {s} {S : Set s} → Out ⊥ S → S
runLoop (out s) = s
forEach : ∀ {a r s} {@0 A : Set a} {R : Set r} {S : Set s} → List A → S → (A → S → Step R S) → Out R S
forEach [] s f = out s
forEach (x ∷ xs) s f with f x s
... | next s′ = forEach xs s′ f
... | stop s′ = out s′
... | ret r = early r

lt-zero : ∀ {ℓ} {@0 A : Set ℓ} (n : Nat) → (n < 0) ≡ true → A
lt-zero n ()
lt-suc : (n : Nat) → (n < suc n) ≡ true
lt-suc zero = refl
lt-suc (suc n) = lt-suc n
lt-step : (a b f : Nat) → (a < b) ≡ true → (b < suc f) ≡ true → (a < f) ≡ true
lt-step a zero f () q
lt-step a (suc b) zero p q = lt-zero b q
lt-step zero (suc b) (suc f) p q = refl
lt-step (suc a) (suc b) (suc f) p q = lt-step a b f p q
lt-add : (i k : Nat) → (i < (i + suc k)) ≡ true
lt-add zero k = refl
lt-add (suc i) k = lt-add i k
add-suc : (i k : Nat) → (i + suc k) ≡ (suc i + k)
add-suc zero k = refl
add-suc (suc i) k = cong (fun₁ suc) (add-suc i k)

-- for i in range(n): the body also gets the proof that i is below n
forUpTo : ∀ {r s} {R : Set r} {S : Set s} (k i n : Nat) → (i + k) ≡ n → S
          → ((j : Nat) → (j < n) ≡ true → S → Step R S) → Out R S
forUpTo zero i n e s f = out s
forUpTo (suc k) i n e s f with f i (subst (fun₁ (λ m → (i < m) ≡ true)) e (lt-add i k)) s
... | next s′ = forUpTo k (suc i) n (trans (sym (add-suc i k)) e) s′ f
... | stop s′ = out s′
... | ret r = early r
forRange : ∀ {r s} {R : Set r} {S : Set s} (n : Nat) → S → ((i : Nat) → (i < n) ≡ true → S → Step R S) → Out R S
forRange n s f = forUpTo n 0 n refl s f

-- while c: the body must end with a proof that the measure went down
data WStep {r s} (R : Set r) (S : Set s) (m : S → Nat) (s₀ : S) : Set (r ⊔ s) where
  wnext : (s′ : S) → (m s′ < m s₀) ≡ true → WStep R S m s₀
  wstop : S → WStep R S m s₀
  wret : R → WStep R S m s₀
whileFuel : ∀ {r s} {R : Set r} {S : Set s} (fuel : Nat) (c : S → Bool) (m : S → Nat) (s₀ : S)
            → ((s : S) → c s ≡ true → WStep R S m s) → (m s₀ < fuel) ≡ true → Out R S
whileFuel zero c m s₀ body p = lt-zero (m s₀) p
whileFuel (suc f) c m s₀ body p with c s₀ in eq
... | false = out s₀
... | true with body s₀ eq
...   | wnext s′ q = whileFuel f c m s′ body (lt-step (m s′) (m s₀) f q p)
...   | wstop s′ = out s′
...   | wret r = early r
whileDo : ∀ {r s} {R : Set r} {S : Set s} (c : S → Bool) (m : S → Nat) (s₀ : S)
          → ((s : S) → c s ≡ true → WStep R S m s) → Out R S
whileDo c m s₀ body = whileFuel (suc (m s₀)) c m s₀ body (lt-suc (m s₀))

-- ---------------------------------------------------------------------------
-- Optional[A]: nothing is None, just x is x.
isNone : ∀ {a} {@0 A : Set a} → Maybe A → Bool
isNone nothing = true
isNone (just _) = false
instance
  EqMaybe : {A : Set} {{_ : Eq₀ A}} → Eq₀ (Maybe A)
  EqMaybe = record { _==_ = λ { nothing nothing → true ; (just a) (just b) → a == b ; _ _ → false } }
findKey : {K V : Set} {{_ : Eq₀ K}} → K → List (K × V) → Maybe V
findKey k [] = nothing
findKey k ((k′ , v) ∷ r) with k == k′
... | true = just v
... | false = findKey k r
dfind : {K V : Set} {{_ : Eq₀ K}} → Dict K V → K → Maybe V
dfind d k = findKey k (items d)

-- A statement whose value is not used: a call, or the arguments of print. The
-- value is still type-checked.
ignore : ∀ {a} {@0 A : Set a} → A → ⊤
ignore _ = tt
also : ∀ {a b} {@0 A : Set a} {@0 B : Set b} → A → B → ⊤
also _ _ = tt
-- The text of a value that is only ever printed. What print writes is not part
-- of what the checker reasons about, so the text itself is left out.
shown : ∀ {a} {@0 A : Set a} → A → String
shown _ = ""
showBool : Bool → String
showBool true = "True"
showBool false = "False"

-- Other tests that rule out a zero divisor.
ltNotZero : {n : Nat} → (0 < n) ≡ true → (n == 0) ≡ false
ltNotZero {suc n} p = refl
posNotZero : {b : Int} → (pos 0 <ᵢ b) ≡ true → (b == pos 0) ≡ false
posNotZero {pos (suc n)} p = refl
negNotZero : {b : Int} → (b <ᵢ pos 0) ≡ true → (b == pos 0) ≡ false
negNotZero {negsuc n} p = refl

-- ---------------------------------------------------------------------------
-- Measures the checker supplies by itself for the two commonest while loops.
sucs : Nat → Nat → Nat
sucs zero i = i
sucs (suc k) i = suc (sucs k i)
lt-weaken : (a b : Nat) → (a < b) ≡ true → (a < suc b) ≡ true
lt-weaken zero b p = refl
lt-weaken (suc a) zero ()
lt-weaken (suc a) (suc b) p = lt-weaken a b p
ltSucs : (k i : Nat) → (i < suc (sucs k i)) ≡ true
ltSucs zero i = lt-suc i
ltSucs (suc k) i = lt-weaken i (suc (sucs k i)) (ltSucs k i)
monus-below : (a b : Nat) → (monus a b < suc a) ≡ true
monus-below zero zero = refl
monus-below zero (suc b) = refl
monus-below (suc a) zero = lt-suc a
monus-below (suc a) (suc b) = lt-weaken (monus a b) (suc a) (monus-below a b)
-- while i < n: ... i += c    the distance monus n i shrinks
monusUp : (n i j : Nat) → (i < n) ≡ true → (i < j) ≡ true → (monus n j < monus n i) ≡ true
monusUp zero i j () q
monusUp (suc n) i zero p ()
monusUp (suc n) zero (suc j) p q = monus-below n j
monusUp (suc n) (suc i) (suc j) p q = monusUp n i j p q
-- while k > 0: ... k -= c    for an int k, toNat k shrinks
natSub-below : (m c : Nat) → (toNat (natSub m c) < suc m) ≡ true
natSub-below m zero = lt-suc m
natSub-below zero (suc c) = refl
natSub-below (suc m) (suc c) = lt-weaken (toNat (natSub m c)) (suc m) (natSub-below m c)
intDown : (k : Int) (c : Nat) → (pos 0 <ᵢ k) ≡ true → (toNat (k -ᵢ pos (suc c)) < toNat k) ≡ true
intDown (pos zero) c ()
intDown (pos (suc m)) c p = natSub-below m c
intDown (negsuc m) c ()

-- ---------------------------------------------------------------------------
-- More of str. A string is taken apart into its characters and put back.
open import Agda.Builtin.Char using (Char)
import Agda.Builtin.Char as Ch
chars : String → List Char
chars = S.primStringToList
unchars : List Char → String
unchars = S.primStringFromList
-- s[i] is a string of one character
strIndex : (s : String) (i : Nat) → (i < strLen s) ≡ true → String
strIndex s i p = unchars (index (chars s) i p ∷ [])
strSlice : Nat → Nat → String → String
strSlice a b s = unchars (slice a b (chars s))
strTake : Nat → String → String
strTake n s = unchars (take n (chars s))
strDrop : Nat → String → String
strDrop n s = unchars (drop n (chars s))
-- for ch in s
strChars : String → List String
strChars s = mapL (λ c → unchars (c ∷ [])) (chars s)
isPrefix : List Char → List Char → Bool
isPrefix [] ys = true
isPrefix (x ∷ xs) [] = false
isPrefix (x ∷ xs) (y ∷ ys) = Ch.primCharEquality x y ∧ isPrefix xs ys
startsWith : String → String → Bool
startsWith s p = isPrefix (chars p) (chars s)
endsWith : String → String → Bool
endsWith s p = isPrefix (reverse (chars p)) (reverse (chars s))
isInfix : List Char → List Char → Bool
isInfix xs [] = isPrefix xs []
isInfix xs (y ∷ ys) = isPrefix xs (y ∷ ys) ∨ isInfix xs ys
-- a in b
strIn : String → String → Bool
strIn a b = isInfix (chars a) (chars b)
ltChars : List Char → List Char → Bool
ltChars [] [] = false
ltChars [] (y ∷ ys) = true
ltChars (x ∷ xs) [] = false
ltChars (x ∷ xs) (y ∷ ys) with Ch.primCharToNat x < Ch.primCharToNat y
... | true = true
... | false = Ch.primCharEquality x y ∧ ltChars xs ys
strLt : String → String → Bool
strLt a b = ltChars (chars a) (chars b)
strRepeat : String → Nat → String
strRepeat s zero = ""
strRepeat s (suc n) = concat s (strRepeat s n)
strJoin : String → List String → String
strJoin sep [] = ""
strJoin sep (x ∷ []) = x
strJoin sep (x ∷ y ∷ r) = concat x (concat sep (strJoin sep (y ∷ r)))
-- s.split(sep) for a sep that is not empty. Each step uses up a character, so
-- one step per character and one more is enough.
splitGo : Nat → List Char → List Char → List Char → List String
splitGo zero sep cur rest = unchars (reverse cur) ∷ []
splitGo (suc f) sep cur [] = unchars (reverse cur) ∷ []
splitGo (suc f) sep cur (c ∷ r) with isPrefix sep (c ∷ r)
... | true = unchars (reverse cur) ∷ splitGo f sep [] (drop (len sep) (c ∷ r))
... | false = splitGo f sep (c ∷ cur) r
strSplit : String → String → List String
strSplit s sep = splitGo (suc (strLen s)) (chars sep) [] (chars s)
strReplace : String → String → String → String
strReplace s old new = strJoin new (strSplit s old)

-- ---------------------------------------------------------------------------
-- xs[-1], xs.pop(), xs.insert(i, v), and range over ints
lastOk : {n : Nat} → (0 < n) ≡ true → (monus n 1 < n) ≡ true
lastOk {suc n} p = lt-suc n
lastOf : ∀ {a} {@0 A : Set a} (xs : List A) → (0 < len xs) ≡ true → A
lastOf xs p = index xs (monus (len xs) 1) (lastOk {len xs} p)
popped : ∀ {a} {@0 A : Set a} (xs : List A) → (0 < len xs) ≡ true → List A
popped xs p = take (monus (len xs) 1) xs
insertAt : ∀ {a} {@0 A : Set a} → List A → Nat → A → List A
insertAt xs zero v = v ∷ xs
insertAt [] (suc i) v = v ∷ []
insertAt (x ∷ xs) (suc i) v = x ∷ insertAt xs i v
rangeInt : Int → Int → List Int
rangeInt a b = mapL (λ k → a +ᵢ pos k) (range₁ (toNat (b -ᵢ a)))
ltDown : (i n : Nat) → (suc i < n) ≡ true → (i < n) ≡ true
ltDown i zero ()
ltDown zero (suc n) p = refl
ltDown (suc i) (suc n) p = ltDown i n p

-- ---------------------------------------------------------------------------
-- xs[i] = v keeps the length of xs, so a bound that held before still holds.
setAtLen : ∀ {a} {@0 A : Set a} (xs : List A) (i : Nat) (v : A) (p : (i < len xs) ≡ true) {n : Nat}
           → len xs ≡ n → len (setAt xs i v p) ≡ n
setAtLen [] i v ()
setAtLen (x ∷ xs) zero v p e = e
setAtLen (x ∷ xs) (suc i) v p {zero} ()
setAtLen (x ∷ xs) (suc i) v p {suc n} e = cong (fun₁ suc) (setAtLen xs i v p (suc-inj e)) where
  suc-inj : {a b : Nat} → suc a ≡ suc b → a ≡ b
  suc-inj refl = refl
lenLt : (a n m : Nat) → m ≡ n → (a < n) ≡ true → (a < m) ≡ true
lenLt a n m refl p = p
setAtKeeps : ∀ {a} {@0 A : Set a} (xs : List A) (i : Nat) (v : A) (p : (i < len xs) ≡ true) (k : Nat)
             → (k < len xs) ≡ true → (k < len (setAt xs i v p)) ≡ true
setAtKeeps xs i v p k q = lenLt k (len xs) (len (setAt xs i v p)) (setAtLen xs i v p refl) q

-- s.strip(): the characters Python counts as white space
isSpace : Char → Bool
isSpace c = go (Ch.primCharToNat c) where
  go : Nat → Bool
  go n = anyL (((8 < n) ∧ (n < 14)) ∷ ((27 < n) ∧ (n < 33)) ∷ (n N.== 133) ∷ (n N.== 160) ∷ (n N.== 5760)
               ∷ ((8191 < n) ∧ (n < 8203)) ∷ (n N.== 8232) ∷ (n N.== 8233) ∷ (n N.== 8239) ∷ (n N.== 8287)
               ∷ (n N.== 12288) ∷ [])
dropSpaces : List Char → List Char
dropSpaces [] = []
dropSpaces (c ∷ cs) with isSpace c
... | true = dropSpaces cs
... | false = c ∷ cs
strLstrip : String → String
strLstrip s = unchars (dropSpaces (chars s))
strRstrip : String → String
strRstrip s = unchars (reverse (dropSpaces (reverse (chars s))))
strStrip : String → String
strStrip s = strRstrip (strLstrip s)

-- An int used as the end of a slice: -k counts from the end, as Python does.
ibound : Nat → Int → Nat
ibound n (pos k) = k
ibound n (negsuc k) = monus n (suc k)
-- An int used as an index: it has to be 0 or more, and below the length.
intIndex : (i : Int) (n : Nat) → (i <ᵢ pos 0) ≡ false → (i <ᵢ pos n) ≡ true → (toNat i < n) ≡ true
intIndex (pos k) n p q = q
intIndex (negsuc k) n () q

-- a - b on Nats, where b is known not to be larger than a. (Python would give a
-- negative number otherwise, and a Nat cannot hold one.)
natMinus : (a b : Nat) → (a < b) ≡ false → Nat
natMinus a b p = monus a b
posNotBelowOne : {a : Nat} → (0 < a) ≡ true → (a < 1) ≡ false
posNotBelowOne {suc a} p = refl
monus-keeps : (a b n : Nat) → (a < n) ≡ true → (monus a b < n) ≡ true
monus-keeps a zero n q = q
monus-keeps zero (suc b) n q = q
monus-keeps (suc a) (suc b) zero ()
monus-keeps (suc a) (suc b) (suc n) q = lt-weaken (monus a b) n (monus-keeps a b n q)
minusBelow : (a b n : Nat) (p : (a < b) ≡ false) → (a < n) ≡ true → (natMinus a b p < n) ≡ true
minusBelow a b n p q = monus-keeps a b n q

-- each part of `a and b` holds when the whole does
andLeft : {a b : Bool} → (a ∧ b) ≡ true → a ≡ true
andLeft {true} p = refl
andRight : {a b : Bool} → (a ∧ b) ≡ true → b ≡ true
andRight {true} p = p
ltAsym : (a b : Nat) → (a < b) ≡ true → (b < a) ≡ false
ltAsym zero zero ()
ltAsym zero (suc b) p = refl
ltAsym (suc a) zero ()
ltAsym (suc a) (suc b) p = ltAsym a b p
ltLower : (k m n : Nat) → (m < k) ≡ false → (m < n) ≡ true → (k < n) ≡ true
ltLower zero m zero p ()
ltLower zero m (suc n) p q = refl
ltLower (suc k) zero n () q
ltLower (suc k) (suc m) zero p ()
ltLower (suc k) (suc m) (suc n) p q = ltLower k m n p q

-- `a and b` where b may rely on a having been true, and `a or b` where b may rely
-- on a having been false, as when Python skips b.
andThen : (a : Bool) → (a ≡ true → Bool) → Bool
andThen true f = f refl
andThen false f = false
orElse : (a : Bool) → (a ≡ false → Bool) → Bool
orElse true f = true
orElse false f = f refl
andThenL : {a : Bool} {f : a ≡ true → Bool} → andThen a f ≡ true → a ≡ true
andThenL {true} p = refl
andThenR : {a : Bool} {f : a ≡ true → Bool} (p : andThen a f ≡ true) → f (andThenL {a} {f} p) ≡ true
andThenR {true} p = p
unNotFalse : {c : Bool} → not c ≡ false → c ≡ true
unNotFalse {true} p = refl

-- ---------------------------------------------------------------------------
-- Exceptions. An exception is the name of its class and its message. A def
-- that can raise gives back either its value (ok) or an exception (err).
record Exn : Set where
  constructor exn
  field
    kind : String
    message : String
data Exc {a} (A : Set a) : Set a where
  ok : A → Exc A
  err : Exn → Exc A
