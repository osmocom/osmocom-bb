#!/usr/bin/env python

import sys

def parse(s):
	return set([x.strip() for x in s.split() if x.strip()])


def solve(board_envs, app_envs):
	if not app_envs:
		return board_envs

	envs = set()

	if '*' in app_envs:
		envs.update(board_envs)
		app_envs.discard('*')

	for e in app_envs:
		if e.startswith('-'):
			envs.discard(e[1:])
		elif e in board_envs:
			envs.add(e)

	return envs


def main(name, board_envs, app_envs):
	# Parse args
	board_envs = parse(board_envs)
	app_envs   = parse(app_envs)

	# Solve
	envs = solve(board_envs, app_envs)

	# Result
	print ' '.join(envs)


if __name__ == '__main__':
	main(*sys.argv)
