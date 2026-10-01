;;; Sierra Script 1.0 - (do not remove this comment)
(script# 970)
(include sci.sh)
(use System)

; Sends that Sierra's compiler gives, and that this compiler took as errors:
; a &rest in the parameters of a send whose target calls (PQ2 Main:
; ((ScriptID param1) notify: &rest)), and a property sent with more than
; one value (Longbow: a talker's loop: with five values). The decompiled
; text compiles to the same code, with a warning.
(public
	c5RestWithACallInTheTarget 0
)

(procedure (c5RestWithACallInTheTarget param1 param2)
	(asm
		pushi #init
		push0
		&rest param2
		push1
		lsp param1
		callk ScriptID, 2
		send 4
		ret
	)
)

(instance c5Script of Script
	(properties)

	(method (doit)
		(asm
			pushi #state
			push2
			pushi 1
			pushi 2
			self 8
			ret
		)
	)
)
