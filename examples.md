## `&exists`
`&exists` serves to make the processing of optional arguments clearer.
```clojure
(method (posn theX theY theZ)
	(if (>= argc 1)
		(= x theX)
		(if (>= argc 2)
			(= y theY)
			(if (>= argc 3) (= z theZ))
		)
	)
	(BaseSetter self)
	(self forceUpd:)
)
```
```clojure
(method (posn theX theY theZ)
	(if (&exists theX)
		(= x theX)
		(if (&exists theY)
			(= y theY)
			(if (&exists theZ)
				(= z theZ)
			)
		)
	)
	(BaseSetter self)
	(self forceUpd:)
)
```

## `foreach`
```clojure
; No need to predeclare n, a &tmp variable is made for you.
(foreach n anArray
	; (do something with n)
)

; Works on anything based on the List kernel calls.
(foreach item aCollection
	; (do something with item)
)
```

## `verbs`
The `verbs` block expands into a standard SCI11 `doVerb` method, as a way to write them in a more terse style.
```clojure
(instance anExample of Prop
	(verbs
		(V_TALK
			(++ exampleTalks)
			(StartConversation 124)
		)
		(V_DO (gRoom setScript: TouchExampleScript))
		(V_ANITEM (Print "You get the idea."))
	)
)
```
Once expanded, an `else` handler to call the superclass' `doVerb` is thrown in for free.

## `&getpoly`
SCI Companion's polygon editor is great, but the way the data it produces is used results in a major code smell.
Polygons take local variable space, and there are three methods to process them for use. The `&getpoly` keyword
lets you produce polygon-loading bytecode that's nigh-indistinguishable from what you'd see if you decompile a
random Sierra game (assuming that game is new enough to *use* polygons).

To use `&getpoly`, give each individual polygon a distinct name (the blank string counts) and *remove the
`(include ___.shp)` line* from your room's script. Next, replace the `(AddPolygonsToRoom)` call in your room's
`init` with something like this:
```lisp
(gRoom addObstacle: (&getpoly "room"))
```
The default polygon, with a blank name field, listed as "Default", and named `P_Default#` in the `.shp` file, can be addressed with just `(&getpoly "")`.
You may pass as many `(&getpoly "...")` calls to `addObstacle` at once as you'd like. Once all your rooms have
been converted at your leisure, you can safely remove `AddPolygonsToRoom` and its support procedures from your
*Main* script.
