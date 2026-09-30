;;; Sierra Script 1.0 - (do not remove this comment)
(script# 947)
(include sci.sh)

; A last case with a value and no body: (switch param1 (1 (= temp0 5)) (2)).
; Sierra's compiler emits no bnt for it: its eq? goes on to the toss.
(public
	s3EmptyLastCase 0
)

(procedure (s3EmptyLastCase param1 &tmp temp0)
	(asm
		lsp param1
		dup
		ldi 1
		eq?
		bnt case2
		ldi 5
		sat temp0
		jmp done
	case2:
		dup
		ldi 2
		eq?
	done:
		toss
		ret
	)
)
