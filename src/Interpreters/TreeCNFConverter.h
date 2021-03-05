#pragma once

#include <Parsers/IAST_fwd.h>
#include <Parsers/ASTLiteral.h>
#include <vector>
#include <set>
#include <unordered_map>

namespace DB
{

class CNFQuery
{
public:
    struct AtomicFormula
    {
        bool negative = false;
        ASTPtr ast;

        /// for set
        bool operator<(const AtomicFormula & rhs) const
        {
            return ast == rhs.ast ? negative < rhs.negative : ast < rhs.ast;
        }
    };

    using OrGroup = std::set<AtomicFormula>;
    using AndGroup = std::set<OrGroup>;

    CNFQuery(AndGroup && statements_) : statements(std::move(statements_)) { }

    template <typename P>
    CNFQuery & filterAlwaysTrueGroups(P predicate_is_unknown)  /// delete always true groups
    {
        AndGroup filtered;
        for (const auto & or_group : statements)
        {
            if (predicate_is_unknown(or_group))
                filtered.insert(or_group);
        }
        std::swap(statements, filtered);
        return *this;
    }

    template <typename P>
    CNFQuery & filterAlwaysFalseAtoms(P predicate_is_unknown)  /// delete always false atoms
    {
        AndGroup filtered;
        for (const auto & or_group : statements)
        {
            OrGroup filtered_group;
            for (auto ast : or_group)
            {
                if (predicate_is_unknown(ast))
                    filtered_group.insert(ast);
            }
            if (!filtered_group.empty())
                filtered.insert(filtered_group);
            else
            {
                /// all atoms false -> group false -> CNF   false
                filtered.clear();
                filtered_group.clear();
                filtered_group.insert(AtomicFormula{false, std::make_shared<ASTLiteral>(static_cast<UInt8>(0))});
                filtered.insert(filtered_group);
                std::swap(statements, filtered);
                return *this;
            }
        }
        std::swap(statements, filtered);
        return *this;
    }

    template <typename F>
    CNFQuery & transformGroups(F func)
    {
        AndGroup result;
        for (const auto & group : statements)
        {
            auto new_group = func(group);
            if (!new_group.empty())
                result.insert(std::move(new_group));
        }
        std::swap(statements, result);
        return *this;
    }

    template <typename F>
    CNFQuery & transformAtoms(F func)
    {
        transformGroups([func](const OrGroup & group) -> OrGroup
                        {
                            OrGroup result;
                            for (const auto & atom : group)
                            {
                                auto new_atom = func(atom);
                                if (new_atom.ast)
                                    result.insert(std::move(new_atom));
                            }
                            return result;
                        });
        return *this;
    }

    const AndGroup & getStatements() const { return statements; }

    std::string dump() const;

    /// Converts != -> NOT =; <,>= -> (NOT) <; >,<= -> (NOT) <= for simpler matching
    CNFQuery & pullNotOutFunctions();
    /// Revert pullNotOutFunctions actions
    CNFQuery & pushNotInFuntions();

private:
    AndGroup statements;
};

class TreeCNFConverter
{
public:

    static CNFQuery toCNF(const ASTPtr & query);

    static ASTPtr fromCNF(const CNFQuery & cnf);
};

void pushNotIn(CNFQuery::AtomicFormula & atom);

}
