#include "symbols.hpp"

#include <unordered_map>

namespace tex::detail {
namespace {

using Table = std::unordered_map<std::string_view, Symbol>;

void insert(Table& table, Role role, std::string_view items) {
    while (!items.empty()) {
        auto divider = items.find('=');
        auto end = items.find(' ', divider + 1);
        table.emplace(items.substr(0, divider), Symbol{items.substr(divider + 1, end - divider - 1), role});
        if (end == items.npos) break;
        items.remove_prefix(end + 1);
    }
}

}

std::optional<Symbol> symbol(std::string_view name) {
    static const Table symbols = [] {
        Table result;
        insert(result, Role::identifier,
               "alpha=α beta=β gamma=γ delta=δ epsilon=ϵ varepsilon=ε zeta=ζ eta=η theta=θ vartheta=ϑ "
               "iota=ι kappa=κ varkappa=ϰ lambda=λ mu=μ nu=ν xi=ξ omicron=ο pi=π varpi=ϖ rho=ρ varrho=ϱ "
               "sigma=σ varsigma=ς tau=τ upsilon=υ phi=ϕ varphi=φ chi=χ psi=ψ omega=ω "
               "digamma=ϝ imath=ı jmath=ȷ partial=∂ ell=ℓ hbar=ℏ hslash=ℏ wp=℘");
        insert(result, Role::constant,
               "Gamma=Γ Delta=Δ Theta=Θ Lambda=Λ Xi=Ξ Pi=Π Sigma=Σ Upsilon=Υ Phi=Φ Psi=Ψ Omega=Ω "
               "Re=ℜ Im=ℑ aleph=ℵ beth=ℶ gimel=ℷ daleth=ℸ emptyset=∅ varnothing=∅ top=⊤ bot=⊥ "
               "square=□ Box=□ blacksquare=■ lozenge=◊ Diamond=◊ checkmark=✓ degree=° "
               "clubsuit=♣ diamondsuit=♢ heartsuit=♡ spadesuit=♠");
        insert(result, Role::oper,
               "infty=∞ nabla=∇ forall=∀ exists=∃ nexists=∄ neg=¬ lnot=¬ angle=∠ measuredangle=∡ triangle=△ "
               "prime=′ backprime=‵ surd=√ pm=± mp=∓ times=× div=÷ cdot=⋅ ast=∗ star=⋆ circ=∘ bullet=∙ "
               "oplus=⊕ ominus=⊖ otimes=⊗ oslash=⊘ odot=⊙ wr=≀ cup=∪ cap=∩ uplus=⊎ sqcup=⊔ sqcap=⊓ "
               "wedge=∧ land=∧ vee=∨ lor=∨ setminus=∖ smallsetminus=∖ amalg=⨿ dagger=† ddagger=‡ "
               "in=∈ ni=∋ owns=∋ notin=∉ le=≤ leq=≤ leqslant=⩽ ge=≥ geq=≥ geqslant=⩾ ne=≠ neq=≠ "
               "equiv=≡ sim=∼ simeq=≃ approx=≈ cong=≅ asymp=≍ propto=∝ doteq=≐ triangleq=≜ coloneqq=≔ "
               "ll=≪ gg=≫ lesssim=≲ gtrsim=≳ prec=≺ succ=≻ preceq=⪯ succeq=⪰ subset=⊂ supset=⊃ "
               "subseteq=⊆ supseteq=⊇ subsetneq=⊊ supsetneq=⊋ nsubseteq=⊈ nsupseteq=⊉ nmid=∤ "
               "sqsubset=⊏ sqsupset=⊐ sqsubseteq=⊑ sqsupseteq=⊒ parallel=∥ nparallel=∦ perp=⊥ mid=∣ "
               "vdash=⊢ dashv=⊣ models=⊨ vDash=⊨ Vdash=⊩ bowtie=⋈ smile=⌣ frown=⌢ therefore=∴ because=∵ "
               "implies=⟹ impliedby=⟸ iff=⟺ nrightarrow=↛ nleftarrow=↚ nleftrightarrow=↮ "
               "nearrow=↗ searrow=↘ swarrow=↙ nwarrow=↖ leftharpoonup=↼ leftharpoondown=↽ "
               "rightharpoonup=⇀ rightharpoondown=⇁ leftrightharpoons=⇋ rightleftharpoons=⇌ "
               "curvearrowleft=↶ curvearrowright=↷ twoheadrightarrow=↠ twoheadleftarrow=↞ "
               "ldots=… dots=… dotsc=… cdots=⋯ dotsb=⋯ dotsm=⋯ dotsi=⋯ dotso=… vdots=⋮ ddots=⋱ iddots=⋰ "
               "flat=♭ natural=♮ sharp=♯ %=% #=# &=& _=_ $=$");
        insert(result, Role::delimiter,
               "leftarrow=← gets=← rightarrow=→ to=→ leftrightarrow=↔ Leftarrow=⇐ Rightarrow=⇒ Leftrightarrow=⇔ "
               "longleftarrow=⟵ longrightarrow=⟶ longleftrightarrow=⟷ Longleftarrow=⟸ Longrightarrow=⟹ Longleftrightarrow=⟺ "
               "mapsto=↦ longmapsto=⟼ hookleftarrow=↩ hookrightarrow=↪ uparrow=↑ downarrow=↓ updownarrow=↕ "
               "Uparrow=⇑ Downarrow=⇓ Updownarrow=⇕ langle=⟨ rangle=⟩ lbrace={ rbrace=} lbrack=[ rbrack=] "
               "lgroup=⟮ rgroup=⟯ lvert=| rvert=| vert=| lVert=‖ rVert=‖ Vert=‖ lceil=⌈ rceil=⌉ "
               "lfloor=⌊ rfloor=⌋ backslash=∖ |=‖ {={ }=}");
        insert(result, Role::limits,
               "sum=∑ prod=∏ coprod=∐ bigcup=⋃ bigcap=⋂ biguplus=⨄ bigvee=⋁ bigwedge=⋀ bigsqcup=⨆ "
               "bigoplus=⨁ bigotimes=⨂ bigodot=⨀ lim=lim sup=sup inf=inf min=min max=max gcd=gcd det=det Pr=Pr intop=∫");
        insert(result, Role::large, "int=∫ iint=∬ iiint=∭ iiiint=⨌ oint=∮ oiint=∯ oiiint=∰");
        insert(result, Role::function,
               "sin=sin cos=cos tan=tan cot=cot sec=sec csc=csc arcsin=arcsin arccos=arccos arctan=arctan "
               "sinh=sinh cosh=cosh tanh=tanh coth=coth log=log ln=ln lg=lg exp=exp arg=arg ker=ker "
               "dim=dim hom=hom deg=deg mod=mod bmod=mod");
        result.emplace("liminf", Symbol{"lim inf", Role::limits});
        result.emplace("limsup", Symbol{"lim sup", Role::limits});
        result.emplace("injlim", Symbol{"inj lim", Role::limits});
        result.emplace("projlim", Symbol{"proj lim", Role::limits});
        return result;
    }();
    if (auto match = symbols.find(name); match != symbols.end()) return match->second;
    return std::nullopt;
}

}
