;;; Sierra Script 1.0 - (do not remove this comment)
(script# 946)
(include sci.sh)

; A case value with a branch:
; (switch param1 ((if param2 1 else 2) (= temp0 5)) (3 (= temp0 6))).
; The value of the case is [dup, eq?], with an if inside it.
(public
	s2CaseValueBranch 0
)

(procedure (s2CaseValueBranch param1 param2 &tmp temp0)
	(asm
		lsp param1
		dup
		lap param2
		bnt value2
		ldi 1
		jmp valueDone
	value2:
		ldi 2
	valueDone:
		eq?
		bnt case2
		ldi 5
		sat temp0
		jmp done
	case2:
		dup
		ldi 3
		eq?
		bnt done
		ldi 6
		sat temp0
	done:
		toss
		ret
	)
)
