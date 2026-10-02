#pragma once

#include <string>

// Shapes of bytecode for the tests of the scope engine: the asm of each
// dialect, and the one region tree of the shape.
namespace shapes
{
	// (if a (return 1) else (return 2)) (return 3): the jmp before the else
	// is dead after a ret, and the code after the last ret is dead.
	inline const char *const DeadCode = R"(
			lap 1
			bnt else
			ldi 1
			ret
			jmp end
		else:
			ldi 2
			ret
		end:
			ldi 3
			ret
		)";
	inline const std::string DeadCodeTree =
		"code 0000\n"
		"if 0001\n"
		"  then\n"
		"    code 0002-0003\n"
		"  else 0004\n"
		"    code 0005-0006\n"
		"code 0007-0008\n";

	// (if a (if b X) else Y). Sierra's compiler threads the bnt of the
	// inner if to the end of the outer if.
	inline const char *const NestedIfPlain = R"(
			lap 1
			bnt else
			lap 2
			bnt innerEnd
			ldi 1
			sat 0
		innerEnd:
			jmp end
		else:
			ldi 2
			sat 0
		end:
			ret
		)";
	inline const char *const NestedIfSierra = R"(
			lap 1
			bnt else
			lap 2
			bnt end
			ldi 1
			sat 0
			jmp end
		else:
			ldi 2
			sat 0
		end:
			ret
		)";
	inline const std::string NestedIfTree =
		"code 0000\n"
		"if 0001\n"
		"  then\n"
		"    code 0002\n"
		"    if 0003\n"
		"      then\n"
		"        code 0004-0005\n"
		"  else 0006\n"
		"    code 0007-0008\n"
		"code 0009\n";

	// (if (and a b) X else Y): plain, the first bnt goes to the second.
	inline const char *const AndPlain = R"(
			lap 1
			bnt join
			lap 2
		join:
			bnt else
			ldi 1
			jmp end
		else:
			ldi 2
		end:
			ret
		)";
	inline const char *const AndThreaded = R"(
			lap 1
			bnt else
			lap 2
			bnt else
			ldi 1
			jmp end
		else:
			ldi 2
		end:
			ret
		)";
	inline const std::string AndTree =
		"code 0000\n"
		"if 0001 0003\n"
		"  term\n"
		"    code 0002\n"
		"  then\n"
		"    code 0004\n"
		"  else 0005\n"
		"    code 0006\n"
		"code 0007\n";

	// (if (or a b) X): Sierra's bt goes to the bnt of the if; this
	// repository's compiler's bt goes past it, to the then-part.
	inline const char *const OrSierra = R"(
			lap 1
			bt test
			lap 2
		test:
			bnt end
			ldi 1
		end:
			ret
		)";
	inline const char *const OrCompanion = R"(
			lap 1
			bt then
			lap 2
			bnt end
		then:
			ldi 1
		end:
			ret
		)";
	inline const std::string OrTree =
		"code 0000\n"
		"or 0001\n"
		"  code 0002\n"
		"if 0003\n"
		"  then\n"
		"    code 0004\n"
		"code 0005\n";

	// (if (or (and a b) c) X): Sierra's bnt goes to the bt of the or;
	// this repository's compiler's bnt goes past it, to c.
	inline const char *const OrAndSierra = R"(
			lap 1
			bnt orBt
			lap 2
		orBt:
			bt test
			lap 3
		test:
			bnt end
			ldi 1
		end:
			ret
		)";
	inline const char *const OrAndCompanion = R"(
			lap 1
			bnt c
			lap 2
			bt then
		c:
			lap 3
			bnt end
		then:
			ldi 1
		end:
			ret
		)";
	inline const std::string OrAndTree =
		"code 0000\n"
		"if 0001\n"
		"  then\n"
		"    code 0002\n"
		"or 0003\n"
		"  code 0004\n"
		"if 0005\n"
		"  then\n"
		"    code 0006\n"
		"code 0007\n";

	// (= t (or a (and b c))): both branches go to the store.
	inline const char *const ValueOrAnd = R"(
			lap 1
			bt store
			lap 2
			bnt store
			lap 3
		store:
			sat 0
			ret
		)";
	inline const std::string ValueOrAndTree =
		"code 0000\n"
		"or 0001\n"
		"  code 0002\n"
		"  if 0003\n"
		"    then\n"
		"      code 0004\n"
		"code 0005-0006\n";

	// (while a (if b (break)) (if c (continue)) (if d X)): Sierra's
	// compiler threads the bnt of the last if to the head.
	inline const char *const LoopPlain = R"(
		head:
			lap 1
			bnt exit
			lap 2
			bnt noBreak
			jmp exit
		noBreak:
			lap 3
			bnt noContinue
			jmp head
		noContinue:
			lap 4
			bnt latch
			+at 0
		latch:
			jmp head
		exit:
			ret
		)";
	inline const char *const LoopSierra = R"(
		head:
			lap 1
			bnt exit
			lap 2
			bnt noBreak
			jmp exit
		noBreak:
			lap 3
			bnt noContinue
			jmp head
		noContinue:
			lap 4
			bnt head
			+at 0
			jmp head
		exit:
			ret
		)";
	inline const std::string LoopTree =
		"loop 0000 latch 000b\n"
		"  body\n"
		"    code 0000\n"
		"    if 0001\n"
		"      then\n"
		"        code 0002\n"
		"        if 0003\n"
		"          then\n"
		"            break 1 0004\n"
		"        code 0005\n"
		"        if 0006\n"
		"          then\n"
		"            continue 1 0007\n"
		"        code 0008\n"
		"        if 0009\n"
		"          then\n"
		"            code 000a\n"
		"      else break 1\n"
		"code 000c\n";

	// (switch x (1 A) (2 B) (else C)).
	inline const char *const Switch = R"(
			lsp 1
			dup
			ldi 1
			eq?
			bnt case2
			ldi 5
			sat 0
			jmp done
		case2:
			dup
			ldi 2
			eq?
			bnt caseElse
			ldi 6
			sat 0
			jmp done
		caseElse:
			ldi 7
			sat 0
		done:
			toss
			ret
		)";
	inline const std::string SwitchTree =
		"switch 0000 toss 0011\n"
		"  case bnt 0004 jmp 0007\n"
		"    value\n"
		"      code 0001-0003\n"
		"    body\n"
		"      code 0005-0006\n"
		"  case bnt 000b jmp 000e\n"
		"    value\n"
		"      code 0008-000a\n"
		"    body\n"
		"      code 000c-000d\n"
		"  case\n"
		"    body\n"
		"      code 000f-0010\n"
		"code 0012\n";

	// (if (< a 5 10) X): the bnt inside the chain is inert. Sierra's
	// compiler threads it to the end of the if; plain, it goes to the
	// bnt of the if.
	inline const char *const NaryPlain = R"(
			lsp 1
			ldi 5
			lt?
			bnt test
			pprev
			ldi 10
			lt?
		test:
			bnt end
			+at 0
		end:
			ret
		)";
	inline const char *const NarySierra = R"(
			lsp 1
			ldi 5
			lt?
			bnt end
			pprev
			ldi 10
			lt?
			bnt end
			+at 0
		end:
			ret
		)";
	inline const std::string NaryTree =
		"code 0000-0006\n"
		"if 0007\n"
		"  then\n"
		"    code 0008\n"
		"code 0009\n";

	// A while in a while; the inner one breaks out of both.
	inline const char *const NestedLoops = R"(
		outer:
			lap 1
			bnt outerExit
		inner:
			lap 2
			bnt innerExit
			lap 3
			bnt skip
			jmp outerExit
		skip:
			jmp inner
		innerExit:
			jmp outer
		outerExit:
			ret
		)";
	inline const std::string NestedLoopsTree =
		"loop 0000 latch 0008\n"
		"  body\n"
		"    code 0000\n"
		"    if 0001\n"
		"      then\n"
		"        loop 0002 latch 0007\n"
		"          body\n"
		"            code 0002\n"
		"            if 0003\n"
		"              then\n"
		"                code 0004\n"
		"                if 0005\n"
		"                  then\n"
		"                    break 2 0006\n"
		"              else break 1\n"
		"      else break 1\n"
		"code 0009\n";

	// A switch in a loop whose first case jumps to the toss.
	inline const char *const SwitchInLoop = R"(
		head:
			lap 1
			bnt exit
			lsp 2
			dup
			ldi 1
			eq?
			bnt done
			+at 0
			jmp done
		done:
			toss
			jmp head
		exit:
			ret
		)";
}
