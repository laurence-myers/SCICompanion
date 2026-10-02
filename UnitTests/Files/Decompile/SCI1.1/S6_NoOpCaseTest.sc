;;; Sierra Script 1.0 - (do not remove this comment)
(script# 963)
(include sci.sh)

; A case whose bnt goes to the next instruction: its body runs for each
; value that no case before it takes. It is the else case, and the compare
; is a statement. Shape of PQ1 VGA, script 141 (uniform::doVerb).
(public
	s6NoOpCaseTest 0
)

(procedure (s6NoOpCaseTest param1 &tmp temp0)
	(asm
		lsp param1
		dup
		ldi 4
		eq?
		bnt case2
		ldi 5
		sat temp0
		jmp done
	case2:
		dup
		ldi 1
		eq?
		bnt body
	body:
		ldi 7
		sat temp0
	done:
		toss
		ret
	)
)
