static void open_outfile(CompilerContext *context, char *infilename) {
  // main.c => main.o, but also handle special cases
  char *infile_dot = strrchr(infilename, '.');
  int outfilename_sz = strlen(infilename) + 3;
  char *outfilename = malloc(outfilename_sz);
  DIE_UNLESS(outfilename, "Could not allocate outfilename")
  if (infile_dot) {
    *infile_dot = '\0';
  }
  strncpy(outfilename, infilename, outfilename_sz - 3);
  strncat(outfilename, ".s", 2);

  fprintf(stderr, "DEBUG: outfilename=%s\n", outfilename);
  
  context->outfile = fopen(outfilename, "w");
  DIE_UNLESS(context->outfile, "Could not open output file");
}


