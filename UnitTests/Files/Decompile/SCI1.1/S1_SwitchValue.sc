;;; Sierra Script 1.0 - (do not remove this comment)
(script# 945)
(include sci.sh)

; A switch as a value: (= temp0 (switch param1 (1 10) (2 20) (else 30))).
; Each case body leaves its value in the accumulator, and the toss keeps it.
(public
	s1SwitchValue 0
)

(procedure (s1SwitchValue param1 &tmp temp0)
	(asm
		lsp param1
		dup
		ldi 1
		eq?
		bnt case2
		ldi 10
		jmp done
	case2:
		dup
		ldi 2
		eq?
		bnt caseElse
		ldi 20
		jmp done
	caseElse:
		ldi 30
	done:
		toss
		sat temp0
		ret
	)
)
