# Contributing to Taiwins

Taiwins is hosted on github, contributing to taiwins involves sending pull
requests and submitting issues. From the beginning, taiwins has been quietly
under my solo development, so the git history is far from clean, but for now, I
am creating a common practice to facilitate the collaboration.

## submitting issues

## Pull requests

There are 3 lines command to start;

	1. git clone https://github.com/taiwins/taiwins && cd taiwins
	2. git remote rename origin upstream.
	3. git checkout -b your-awesome-new-feature
	
Then you after a few moment of development and you think it is mature enough,
it is time to create your pull request. You do not need to finish the whole
development before submitting a PR, simple start your PR title with
*WIP:*. Actually I would encourage you to create Work-in-progress PR early, so
we could kick off the discussion and minimize the waste the developing
time.

### commit message

Start with a short, explicit title would be a good idea. Taiwins is modular in
design, and it splitted into server and client code. As you can see in the
source tree, on the server side, can find `backend`, `dbus`, `compositor`,
`config`, `desktop`, etc. Client now has `desktop_shell`, `desktop_console`,
`widget` and `update_icon_cache`. Signing-off is recommended so we can count the
contribution. An good example of commit message would be like this:
	
	config: adding new reload shell binding.
	
	state the purpose, reason of the commit. Summary about 
	want has been changed.
	
	Signed-off-by: John Doe <J.Doe@abcdefg.org>

Signing-off is done through `git commit -s`.

### code review

After engaging with some good discussion and you decide to make some changes,
you can rebase your work with `git rebase -i` to apply the changes, then force
push to your remote. This will greatly help the merging process. In general,
there are 4 steps

1. **Review** the commit messages.

2. **Review** the code style and correctness, tests maybe run if appliable.

3. **Modifier** the commits, following the discussion in the review.

5. **Merge** the patch when review is approved.

## Coding Style reference

Taiwins style is similar to the
[kernel](https://www.kernel.org/doc/Documentation/process/coding-style.rst)
style. This means:

- We uses gnu11 standard, but very limited GNU extensions are used.
- we use the same **tabs, space** rule(indent with tabs, 8 characters wide);
- opening braces on the same line on if statments, no braces for one-statement
  if body.
- opening braces is the one the next line for functions.
- cases are aligned with switch.
- try to keep the lines within 80 characters wide;

But there are some differences.
- to make easy for line width, we use the following declaration and definition
  style to split the return type and function name in two different lines.

```c
/* declaration */
void
function_a(int x, int y, int z);

/* definition */
void
function_a(int x, int y, int z)
{
	int a, b, c;
	
	//comment
	if (x < 10)
		a = x;
	else if (a > 100 ) {
		switch (y) {
		case 'a':
			break;
		case 'b':
			break;
		default:
			break;
		}
	}
}

void
function_is_very_very_lone(very_complex_type_name_a a,
                           very_complex_type_name_b b);
```

- As in the example, we use c comment style outside the function body, and c++
  comment style inside, this would make it easier temporarily comment out a
  function.

- We also use both space and tabs to align the code, in the example above, for
  long arguments which span multiple lines, we use spaces to align the
  parameters with opening parentheses. This would keep the code properly aigned
  under different tab width.
  
## Wikis for getting started.

At the moment documentation is generated through doxygen. Also, previous notes
registered during the development is under `docs/. For start, you can check out
the [mainpage](docs/mainpage.md).
