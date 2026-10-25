# Show the current directory in interactive BusyBox ash sessions.
case $- in
  *i*) PS1='${PWD} # ' ;;
esac
